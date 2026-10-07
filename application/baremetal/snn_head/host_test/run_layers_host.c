/*
 * run_layers_host.c - host coverage for the production RuntimeSession /
 * ThreadRunner path across the SNN Head layer artifacts.
 *
 * The mock supplies deterministic DATA or VOLTAGE frames for each completion
 * barrier. This exercises the real input encoder, per-timestep runner schedule,
 * output decoder, and caller-owned buffers under ASan/UBSan; it is not a
 * PAICORE numerical or NoC simulation.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "snn_head_internal.h"

int snn_head_host_load_artifacts(const char *asset_dir);
int host_enum_output_axons(const uint8_t *buf, unsigned size,
                           uint32_t *elem_to_axon, uint32_t max_elems,
                           uint32_t *out_count);
typedef uint32_t (*host_mock_gen_fn)(void *ctx, uint32_t control_high,
                                     uint32_t control_low, rvrt_frame_t *out,
                                     uint32_t cap);
void host_mock_set_generator(host_mock_gen_fn fn, void *ctx);

#ifndef SNN_HEAD_ASSET_DIR
#define SNN_HEAD_ASSET_DIR "assets"
#endif

#define HOST_WORK_DATA_HIGH 0x80000000U
#define HOST_WORK_VOLTAGE_HIGH 0xA0000000U
#define HOST_COMPLETE_HIGH 0xE0000000U
#define HOST_MOCK_GEN_CAP ((SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM * 4U) + 1U)

typedef struct host_layer_generator_s {
    uint32_t elem_to_axon[SNN_HEAD_HIDDEN_DIM];
    uint32_t elements;
    uint32_t timesteps;
    uint32_t completion_target;
    uint32_t current_target;
    uint32_t next_timestep;
    uint32_t output_target_lcn;
    bool is_voltage;
} host_layer_generator_t;

static uint8_t g_input[SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM];

static inline rvrt_frame_t make_work(uint32_t high, uint32_t axon,
                                     uint32_t timestamp, uint8_t payload)
{
    return (rvrt_frame_t){
        high | (((timestamp >> 7U) & 0x1U) << 28U),
        ((timestamp & 0x7FU) << 17U) | (axon << 8U) | (uint32_t)payload,
    };
}

static uint32_t generate_layer_frames(void *user_data, uint32_t control_high,
                                      uint32_t control_low, rvrt_frame_t *out,
                                      uint32_t capacity)
{
    host_layer_generator_t *const generator =
        (host_layer_generator_t *)user_data;
    uint32_t count = 0U;
    if ((generator == NULL) || (out == NULL) || (capacity == 0U)) {
        return 0U;
    }
    if ((control_high >> 28U) == 0xDU) {
        generator->current_target = 0U;
        generator->next_timestep = 0U;
        out[count++] = make_work(HOST_COMPLETE_HIGH, 0U, 0U, 0U);
        return count;
    }
    if ((control_high >> 28U) != 0xCU) {
        return 0U;
    }

    const uint32_t delta = control_low & 0xFFFFFFU;
    if ((delta == 0U) || (delta > generator->completion_target) ||
        (generator->current_target > generator->completion_target - delta)) {
        return 0U;
    }
    const uint32_t target = generator->current_target + delta;
    for (uint32_t timestep = generator->next_timestep;
         (timestep < target) && (timestep < generator->timesteps); ++timestep) {
        const uint32_t timestamp = timestep << generator->output_target_lcn;
        if (!generator->is_voltage) {
            for (uint32_t element = 0U;
                 (element < generator->elements) && (count + 1U < capacity);
                 ++element) {
                if ((element % generator->timesteps) == timestep) {
                    out[count++] = make_work(HOST_WORK_DATA_HIGH,
                                             generator->elem_to_axon[element],
                                             timestamp, 1U);
                }
            }
        } else {
            const uint32_t value = (timestep + 1U) * 1000U;
            for (uint32_t element = 0U; element < generator->elements;
                 ++element) {
                const uint32_t base = generator->elem_to_axon[element];
                for (uint32_t lane = 0U; (lane < 4U) && (count + 1U < capacity);
                     ++lane) {
                    out[count++] =
                        make_work(HOST_WORK_VOLTAGE_HIGH, base + lane * 8U,
                                  timestamp, (uint8_t)(value >> (lane * 8U)));
                }
            }
        }
    }
    generator->next_timestep = target;
    generator->current_target = target;
    if (count < capacity) {
        out[count++] = make_work(HOST_COMPLETE_HIGH, 0U, 0U, 0U);
    }
    return count;
}

static void host_debug_sink(rv_debug_level_t level, const char *title,
                            const char *function_name, const char *message,
                            void *user_data)
{
    (void)user_data;
    fprintf(stderr, "[rvdbg L%d] %s/%s: %s\n", (int)level, title ? title : "?",
            function_name ? function_name : "?", message ? message : "");
}

static int report(const char *name, rvrt_runtime_status_t status)
{
    const bool ok = status == RVRT_RUNTIME_OK;
    printf("%-8s : %s\n", name, ok ? "OK" : "RUN FAILED");
    return ok ? 0 : 1;
}

static int run_layer(const char *name, const uint8_t *artifact_data,
                     const uint8_t *artifact_size_symbol, bool is_voltage)
{
    const size_t artifact_size = snn_head_artifact_size(artifact_size_symbol);
    snn_head_layer_artifact_context_t context = {0};
    if (!snn_head_read_layer_artifact(artifact_data, artifact_size_symbol,
                                      &context)) {
        return report(name, RVRT_RUNTIME_RUNTIME_ERROR);
    }

    host_layer_generator_t generator = {
        .elements = context.output_view.element_count,
        .timesteps = context.runtime.timesteps,
        .completion_target = context.runtime.completion_sync_timestep,
        .output_target_lcn = context.output_view.target_lcn,
        .is_voltage = is_voltage,
    };
    uint32_t mapped_elements = 0U;
    if ((host_enum_output_axons(artifact_data, (unsigned)artifact_size,
                                generator.elem_to_axon, SNN_HEAD_HIDDEN_DIM,
                                &mapped_elements) != 0) ||
        (mapped_elements != generator.elements)) {
        return report(name, RVRT_RUNTIME_RUNTIME_ERROR);
    }
    host_mock_set_generator(generate_layer_frames, &generator);

    rvrt_runtime_session_t session = {0};
    const rvrt_runtime_session_open_config_t session_config = {
        .artifact_data = artifact_data,
        .artifact_size = artifact_size,
        .frame_buffer = layer_frame_buf,
        .frame_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
    };
    rvrt_runtime_status_t status =
        rvrt_runtime_session_open(&session, &session_config);
    if (status == RVRT_RUNTIME_OK) {
        status = rvrt_runtime_session_configure(&session);
    }

    rvrt_thread_runner_t runner = {0};
    const rvrt_thread_runner_open_config_t runner_config = {
        .session = &session,
        .thread_index = 0U,
        .input_mapping_index = 0U,
        .output_mapping_index = 0U,
        .voltage_state = &fc2_voltage_state[0][0],
        .voltage_state_capacity = SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM,
        .timeout_ms = SNN_HEAD_TIMEOUT_MS,
    };
    if (status == RVRT_RUNTIME_OK) {
        status = rvrt_thread_runner_open(&runner, &runner_config);
    }

    memset(g_input, 0x5A, sizeof(g_input));
    if (status == RVRT_RUNTIME_OK) {
        status = rvrt_thread_runner_run_sample(
            &runner, g_input, sizeof(g_input), 0U, tensor_workspace,
            sizeof(tensor_workspace), 0U);
    }
    if (runner.opened) {
        const rvrt_runtime_status_t close_status =
            rvrt_thread_runner_close(&runner);
        if (status == RVRT_RUNTIME_OK) {
            status = close_status;
        }
    }
    if (session.opened) {
        const rvrt_runtime_status_t close_status =
            rvrt_runtime_session_close(&session);
        if (status == RVRT_RUNTIME_OK) {
            status = close_status;
        }
    }
    host_mock_set_generator(NULL, NULL);
    return report(name, status);
}

int main(void)
{
    rv_debug_set_sink(host_debug_sink, NULL);
    rv_debug_set_level(RV_DEBUG_DEBUG);
    if (snn_head_host_load_artifacts(SNN_HEAD_ASSET_DIR) != 0) {
        fprintf(stderr, "failed to load PAICore artifacts from %s\n",
                SNN_HEAD_ASSET_DIR);
        return 2;
    }
    printf(
        "== snn_head host RuntimeSession/ThreadRunner test (ASan/UBSan) ==\n");

    int failures = 0;
    failures |= run_layer("fc1_lif", snn_head_fc1_lif_artifact_start,
                          snn_head_fc1_lif_artifact_size, false);
    failures |= run_layer("block0", snn_head_block0_lif_artifact_start,
                          snn_head_block0_lif_artifact_size, false);
    failures |= run_layer("block1", snn_head_block1_lif_artifact_start,
                          snn_head_block1_lif_artifact_size, false);
    failures |= run_layer("fc2", snn_head_fc2_artifact_start,
                          snn_head_fc2_artifact_size, true);
    printf("\nHOST RUNTIME LAYERS: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
