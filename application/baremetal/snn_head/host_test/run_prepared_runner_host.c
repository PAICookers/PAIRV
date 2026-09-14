#include "paicore_runner.h"
#include "paicore_runner_internal.h"
#include "snn_head_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int snn_head_host_load_artifacts(const char *asset_dir);
int host_enum_output_axons(const uint8_t *buf, unsigned size,
                           uint32_t *elem_to_axon, uint32_t max_elems,
                           uint32_t *out_count);
int host_offset_output_axons(uint8_t *buf, unsigned size, uint32_t delta);
int host_build_data_artifact(uint8_t *buf, unsigned capacity,
                             unsigned *size_out, uint32_t thread_id,
                             uint32_t output_base, uint32_t layout,
                             uint32_t pipeline_latency);
typedef uint32_t (*host_mock_gen_fn)(void *ctx, uint32_t control_high,
                                     uint32_t control_low, rvrt_frame_t *out,
                                     uint32_t cap);
void host_mock_set_generator(host_mock_gen_fn fn, void *ctx);
void host_mock_set_next_generated_split(uint32_t first_batch_count);
uint32_t host_mock_sent_count(void);

#ifndef SNN_HEAD_ASSET_DIR
#define SNN_HEAD_ASSET_DIR "assets"
#endif

#define CHECK(condition, label)                                                \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", label);                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define HOST_VOLTAGE_HIGH 0xA0000000U
#define HOST_COMPLETE_HIGH 0xE0000000U

typedef struct exact_generator_s {
    uint32_t axons[SNN_HEAD_ACTION_DIM];
    uint32_t target_lcn;
    uint32_t current_target;
    uint32_t fault;
} exact_generator_t;

enum {
    EXACT_FAULT_NONE = 0U,
    EXACT_FAULT_MISSING,
    EXACT_FAULT_DUPLICATE_LANE,
    EXACT_FAULT_DUPLICATE_COMPLETE,
    EXACT_FAULT_WRONG_THREAD,
    EXACT_FAULT_WRONG_KIND,
    EXACT_FAULT_WRONG_ADDRESS,
    EXACT_FAULT_WRONG_TIME,
    EXACT_FAULT_INIT_OUTPUT,
};

static rvrt_frame_t voltage_frame(uint32_t axon, uint32_t lane,
                                  uint32_t timestamp, uint32_t value)
{
    return (rvrt_frame_t){
        HOST_VOLTAGE_HIGH | (((timestamp >> 7U) & 1U) << 28U),
        ((timestamp & 0x7FU) << 17U) | ((axon + lane * 8U) << 8U) |
            ((value >> (lane * 8U)) & 0xFFU),
    };
}

static rvrt_frame_t data_frame(uint32_t axon, uint32_t timestamp,
                               uint32_t value)
{
    return (rvrt_frame_t){
        UINT32_C(0x80000000) | (((timestamp >> 7U) & 1U) << 28U),
        ((timestamp & 0x7FU) << 17U) | (axon << 8U) | value,
    };
}

static uint32_t exact_generator(void *user_data, uint32_t control_high,
                                uint32_t control_low, rvrt_frame_t *out,
                                uint32_t capacity)
{
    exact_generator_t *generator = user_data;
    if ((control_high >> 28U) == 0xDU) {
        generator->current_target = 0U;
        if (generator->fault == EXACT_FAULT_INIT_OUTPUT) {
            out[0] = (rvrt_frame_t){HOST_VOLTAGE_HIGH, 0U};
            out[1] = (rvrt_frame_t){HOST_COMPLETE_HIGH, 0U};
            return 2U;
        }
        out[0] = (rvrt_frame_t){HOST_COMPLETE_HIGH, 0U};
        return 1U;
    }
    if (((control_high >> 28U) != 0xCU) || (capacity < 29U)) {
        return 0U;
    }
    const uint32_t timestep = generator->current_target;
    generator->current_target += control_low & 0xFFFFFFU;
    uint32_t count = 0U;
    if (timestep == 0U) {
        out[count++] = (rvrt_frame_t){HOST_COMPLETE_HIGH, 0U};
    }
    const uint32_t timestamp = timestep << generator->target_lcn;
    for (uint32_t elem = SNN_HEAD_ACTION_DIM; elem > 0U; --elem) {
        for (uint32_t lane = 4U; lane > 0U; --lane) {
            out[count++] = voltage_frame(generator->axons[elem - 1U], lane - 1U,
                                         timestamp, timestep + elem);
        }
    }
    if (timestep != 0U) {
        out[count++] = (rvrt_frame_t){HOST_COMPLETE_HIGH, 0U};
    }
    if (timestep == 0U) {
        switch (generator->fault) {
            case EXACT_FAULT_MISSING:
                --count;
                break;
            case EXACT_FAULT_DUPLICATE_LANE:
                out[2] = out[1];
                break;
            case EXACT_FAULT_DUPLICATE_COMPLETE:
                for (uint32_t i = count; i > 1U; --i) {
                    out[i] = out[i - 1U];
                }
                out[1] = (rvrt_frame_t){HOST_COMPLETE_HIGH, 0U};
                ++count;
                break;
            case EXACT_FAULT_WRONG_THREAD:
                out[0].low = 1U;
                break;
            case EXACT_FAULT_WRONG_KIND:
                out[1].high ^= UINT32_C(1) << 29U;
                break;
            case EXACT_FAULT_WRONG_ADDRESS:
                out[1].low =
                    (out[1].low & ~UINT32_C(0x1FF00)) | (UINT32_C(511) << 8U);
                break;
            case EXACT_FAULT_WRONG_TIME:
                out[1].low |= UINT32_C(1) << 17U;
                break;
            default:
                break;
        }
    }
    return count;
}

typedef struct exact_data_generator_s {
    uint32_t axons[SNN_HEAD_HIDDEN_DIM];
    uint32_t target_lcn;
    uint32_t current_target;
} exact_data_generator_t;

typedef struct tiny_generator_s {
    uint32_t thread_id;
    uint32_t output_base;
} tiny_generator_t;

typedef struct exchange_generator_s {
    uint32_t response_count;
    uint32_t response_high;
} exchange_generator_t;

typedef struct exchange_handler_s {
    uint32_t expected_count;
    uint32_t received_count;
    uint32_t fail_at;
} exchange_handler_t;

static uint32_t exchange_generator(void *user_data, uint32_t request_high,
                                   uint32_t request_low, rvrt_frame_t *out,
                                   uint32_t capacity)
{
    exchange_generator_t *generator = user_data;
    (void)request_high;
    (void)request_low;
    if (capacity < generator->response_count) {
        return 0U;
    }
    for (uint32_t i = 0U; i < generator->response_count; ++i) {
        out[i] = (rvrt_frame_t){generator->response_high, i};
    }
    return generator->response_count;
}

static rvrt_session_status_t exchange_handler(void *user_data,
                                              const rvrt_frame_t *frame,
                                              bool *barrier_complete)
{
    exchange_handler_t *handler = user_data;
    if ((frame == NULL) || (barrier_complete == NULL)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    handler->received_count++;
    if ((handler->fail_at != 0U) &&
        (handler->received_count == handler->fail_at)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    *barrier_complete = handler->received_count == handler->expected_count;
    return RVRT_SESSION_OK;
}

static uint32_t tiny_generator(void *user_data, uint32_t control_high,
                               uint32_t control_low, rvrt_frame_t *out,
                               uint32_t capacity)
{
    tiny_generator_t *generator = user_data;
    if ((control_high >> 28U) == 0xDU) {
        out[0] = (rvrt_frame_t){HOST_COMPLETE_HIGH, generator->thread_id};
        return 1U;
    }
    if (((control_high >> 28U) != 0xCU) || (control_low != 1U) ||
        (capacity < 5U)) {
        return 0U;
    }
    for (uint32_t elem = 0U; elem < 4U; ++elem) {
        out[elem] = data_frame(generator->output_base + elem, 0U, elem & 1U);
    }
    out[4] = (rvrt_frame_t){HOST_COMPLETE_HIGH, generator->thread_id};
    return 5U;
}

static uint32_t exact_data_generator(void *user_data, uint32_t control_high,
                                     uint32_t control_low, rvrt_frame_t *out,
                                     uint32_t capacity)
{
    exact_data_generator_t *generator = user_data;
    if ((control_high >> 28U) == 0xDU) {
        generator->current_target = 0U;
        out[0] = (rvrt_frame_t){HOST_COMPLETE_HIGH, 0U};
        return 1U;
    }
    if (((control_high >> 28U) != 0xCU) ||
        (capacity < SNN_HEAD_HIDDEN_DIM + 1U)) {
        return 0U;
    }
    const uint32_t timestep = generator->current_target;
    generator->current_target += control_low & UINT32_C(0xFFFFFF);
    const uint32_t timestamp = timestep << generator->target_lcn;
    for (uint32_t elem = 0U; elem < SNN_HEAD_HIDDEN_DIM; ++elem) {
        out[elem] = data_frame(generator->axons[elem], timestamp, 0U);
    }
    out[SNN_HEAD_HIDDEN_DIM] = (rvrt_frame_t){HOST_COMPLETE_HIGH, 0U};
    return SNN_HEAD_HIDDEN_DIM + 1U;
}

static void make_fc3_configs(rvrt_paicore_runner_prepare_config_t *prepare,
                             rvrt_paicore_runner_attach_config_t *attach)
{
    static rvrt_frame_t frame_buffer[SNN_HEAD_FRAME_BUF_FRAMES];
    static rvrt_voltage_decode_state_t
        voltage_state[SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM];
    static uint32_t
        coverage[(SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM * 4U + 31U) / 32U];
    *prepare = (rvrt_paicore_runner_prepare_config_t){
        .artifact_data = snn_head_fc3_artifact_start,
        .artifact_size = (size_t)(uintptr_t)snn_head_fc3_artifact_size,
        .voltage_state = voltage_state,
        .voltage_state_capacity = SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM,
        .timeout_ms = SNN_HEAD_TIMEOUT_MS,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_EXACT,
    };
    *attach = (rvrt_paicore_runner_attach_config_t){
        .frame_buffer = frame_buffer,
        .frame_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
        .coverage_bitmap = coverage,
        .coverage_word_capacity =
            (uint32_t)(sizeof(coverage) / sizeof(coverage[0])),
    };
}

static int verify_prepared_lifecycle(void)
{
    rvrt_paicore_runner_prepare_config_t prepare;
    rvrt_paicore_runner_attach_config_t attach;
    make_fc3_configs(&prepare, &attach);
    rvrt_paicore_runner_t runners[SNN_HEAD_LAYER_COUNT];
    memset(runners, 0, sizeof(runners));
    for (uint32_t i = 0U; i < SNN_HEAD_LAYER_COUNT; ++i) {
        CHECK(rvrt_paicore_runner_prepare(&runners[i], &prepare) ==
                  RVRT_SESSION_OK,
              "five independent prepares");
    }
    CHECK(rvrt_paicore_runner_attach(&runners[0], &attach) == RVRT_SESSION_OK,
          "attach prepared runner");
    CHECK(rvrt_paicore_runner_attach(&runners[1], &attach) == RVRT_SESSION_BUSY,
          "single active session");
    CHECK(rvrt_paicore_runner_load_config(&runners[0]) == RVRT_SESSION_OK,
          "load prepared config");
    CHECK(rvrt_paicore_runner_detach(&runners[0]) == RVRT_SESSION_OK,
          "detach preserves prepared runner");
    CHECK(rvrt_paicore_runner_attach(&runners[0], &attach) == RVRT_SESSION_OK,
          "reattach prepared runner");
    CHECK(rvrt_paicore_runner_detach(&runners[0]) == RVRT_SESSION_OK,
          "second detach");
    for (uint32_t i = 0U; i < SNN_HEAD_LAYER_COUNT; ++i) {
        CHECK(rvrt_paicore_runner_release(&runners[i]) == RVRT_SESSION_OK,
              "release runner");
    }
    return 0;
}

static int verify_invalid_transitions(void)
{
    rvrt_paicore_runner_prepare_config_t prepare;
    rvrt_paicore_runner_attach_config_t attach;
    make_fc3_configs(&prepare, &attach);
    rvrt_paicore_runner_t runner = {0};
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "EMPTY runner cannot attach");
    CHECK(rvrt_paicore_runner_load_config(&runner) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "EMPTY runner cannot load config");
    CHECK(rvrt_paicore_runner_detach(&runner) == RVRT_SESSION_RUNTIME_ERROR,
          "EMPTY runner cannot detach");
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare transition test runner");
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "PREPARED runner cannot prepare twice");
    CHECK(rvrt_paicore_runner_load_config(&runner) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "PREPARED runner cannot load config");
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) == RVRT_SESSION_OK,
          "attach transition test runner");
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "ATTACHED runner cannot attach twice");
    CHECK(rvrt_paicore_runner_load_config(&runner) == RVRT_SESSION_OK,
          "first config load succeeds");
    CHECK(rvrt_paicore_runner_load_config(&runner) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "config cannot load twice");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release attached transition runner");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release EMPTY runner is idempotent");
    return 0;
}

static int verify_exact_rejects_delayed_schedule(void)
{
    static uint8_t artifact[4096U] __attribute__((aligned(16)));
    unsigned artifact_size = 0U;
    CHECK(host_build_data_artifact(artifact, sizeof(artifact), &artifact_size,
                                   5U, 32U, 0U, 2U) == 0,
          "build delayed-output artifact");
    rvrt_paicore_runner_prepare_config_t prepare = {
        .artifact_data = artifact,
        .artifact_size = artifact_size,
        .timeout_ms = 100U,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_EXACT,
    };
    rvrt_paicore_runner_t runner = {0};
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "EXACT rejects pipeline latency greater than one");
    prepare.rx_policy = RVRT_PAICORE_RUNNER_RX_SPARSE;
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "sparse runner keeps delayed-output schedule support");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release delayed sparse runner");
    return 0;
}

static int verify_exact_complete_before_voltage(void)
{
    rvrt_paicore_runner_prepare_config_t prepare;
    rvrt_paicore_runner_attach_config_t attach;
    make_fc3_configs(&prepare, &attach);
    rvrt_paicore_runner_t runner = {0};
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare exact voltage runner");
    exact_generator_t generator = {.target_lcn = runner.output_view.target_lcn};
    uint32_t output_count = 0U;
    CHECK(host_enum_output_axons(snn_head_fc3_artifact_start, 85088U,
                                 generator.axons, SNN_HEAD_ACTION_DIM,
                                 &output_count) == 0 &&
              output_count == SNN_HEAD_ACTION_DIM,
          "enumerate fc3 output axons");
    host_mock_set_generator(exact_generator, &generator);
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) == RVRT_SESSION_OK,
          "attach exact voltage runner");
    CHECK(rvrt_paicore_runner_load_config(&runner) == RVRT_SESSION_OK,
          "load exact voltage config");
    const uint32_t sent_after_load = host_mock_sent_count();
    static uint8_t input[SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM];
    int32_t output[SNN_HEAD_TIMESTEPS][SNN_HEAD_ACTION_DIM] = {{0}};
    CHECK(rvrt_paicore_runner_run_sample(
              &runner, input, sizeof(input), SNN_HEAD_HIDDEN_DIM, output,
              sizeof(output),
              SNN_HEAD_ACTION_DIM * sizeof(int32_t)) == RVRT_SESSION_OK,
          "COMPLETE before voltage data still requires full coverage");
    CHECK(host_mock_sent_count() == sent_after_load + 9U,
          "one INIT and eight SYNC controls without config reload");
    rvrt_session_stats_t first_stats = {0};
    CHECK(rvrt_paicore_runner_get_stats(&runner, &first_stats) ==
                  RVRT_SESSION_OK &&
              first_stats.rx_irq_count == 9U,
          "COMPLETE-first exact sample drains each known barrier in one IRQ");
    for (uint32_t timestep = 0U; timestep < SNN_HEAD_TIMESTEPS; ++timestep) {
        for (uint32_t elem = 0U; elem < SNN_HEAD_ACTION_DIM; ++elem) {
            CHECK(output[timestep][elem] == (int32_t)(timestep + elem + 1U),
                  "arbitrary lane order reconstructs exact voltage");
        }
    }
    generator.current_target = 0U;
    CHECK(rvrt_paicore_runner_run_sample(
              &runner, input, sizeof(input), SNN_HEAD_HIDDEN_DIM, output,
              sizeof(output),
              SNN_HEAD_ACTION_DIM * sizeof(int32_t)) == RVRT_SESSION_OK,
          "second sample reuses config and resets timeline with INIT");
    CHECK(host_mock_sent_count() == sent_after_load + 18U,
          "two samples add controls but no configuration frames");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release exact voltage runner");
    host_mock_set_generator(NULL, NULL);
    return 0;
}

static int verify_exact_zero_data(void)
{
    static rvrt_frame_t frame_buffer[SNN_HEAD_FRAME_BUF_FRAMES];
    static uint32_t
        coverage[(SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM + 31U) / 32U];
    rvrt_paicore_runner_prepare_config_t prepare = {
        .artifact_data = snn_head_fc1_lif_artifact_start,
        .artifact_size = (size_t)(uintptr_t)snn_head_fc1_lif_artifact_size,
        .timeout_ms = 10000U,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_EXACT,
    };
    const rvrt_paicore_runner_attach_config_t attach = {
        .frame_buffer = frame_buffer,
        .frame_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
        .coverage_bitmap = coverage,
        .coverage_word_capacity =
            (uint32_t)(sizeof(coverage) / sizeof(coverage[0])),
    };
    rvrt_paicore_runner_t runner = {0};
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare exact DATA runner");
    exact_data_generator_t generator = {
        .target_lcn = runner.output_view.target_lcn,
    };
    uint32_t output_count = 0U;
    CHECK(host_enum_output_axons(snn_head_fc1_lif_artifact_start, 1310992U,
                                 generator.axons, SNN_HEAD_HIDDEN_DIM,
                                 &output_count) == 0 &&
              output_count == SNN_HEAD_HIDDEN_DIM,
          "enumerate FC1 DATA output axons");
    host_mock_set_generator(exact_data_generator, &generator);
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) == RVRT_SESSION_OK,
          "attach exact DATA runner");
    CHECK(rvrt_paicore_runner_load_config(&runner) == RVRT_SESSION_OK,
          "load exact DATA config");
    static uint8_t input[SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM];
    static uint8_t output[SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM];
    memset(output, 0xA5, sizeof(output));
    const rvrt_session_status_t status = rvrt_paicore_runner_run_sample(
        &runner, input, sizeof(input), runner.input_row_bytes, output,
        sizeof(output), SNN_HEAD_HIDDEN_DIM);
    if (status != RVRT_SESSION_OK) {
        fprintf(stderr, "exact DATA status=%s\n",
                rvrt_session_status_string(status));
    }
    CHECK(status == RVRT_SESSION_OK,
          "exact DATA consumes the fixed output-plus-COMPLETE goal");
    for (size_t i = 0U; i < sizeof(output); ++i) {
        CHECK(output[i] == 0U, "exact DATA preserves explicit zero payloads");
    }
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release exact DATA runner");
    host_mock_set_generator(NULL, NULL);
    return 0;
}

static int verify_exact_noncanonical_voltage_mapping(void)
{
    static uint8_t artifact_copy[85088U] __attribute__((aligned(16)));
    memcpy(artifact_copy, snn_head_fc3_artifact_start, sizeof(artifact_copy));
    CHECK(host_offset_output_axons(artifact_copy, sizeof(artifact_copy),
                                   256U) == 0,
          "create valid noncanonical output mapping");
    rvrt_paicore_runner_prepare_config_t prepare;
    rvrt_paicore_runner_attach_config_t attach;
    make_fc3_configs(&prepare, &attach);
    prepare.artifact_data = artifact_copy;
    rvrt_paicore_runner_t runner = {0};
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare accepts legal noncanonical voltage mapping");
    CHECK(!runner.has_fast_voltage_layout,
          "noncanonical mapping selects generic exact decoder");
    exact_generator_t generator = {.target_lcn = runner.output_view.target_lcn};
    uint32_t output_count = 0U;
    CHECK(host_enum_output_axons(artifact_copy, sizeof(artifact_copy),
                                 generator.axons, SNN_HEAD_ACTION_DIM,
                                 &output_count) == 0 &&
              output_count == SNN_HEAD_ACTION_DIM,
          "enumerate offset voltage output axons");
    host_mock_set_generator(exact_generator, &generator);
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) == RVRT_SESSION_OK,
          "attach noncanonical voltage runner");
    CHECK(rvrt_paicore_runner_load_config(&runner) == RVRT_SESSION_OK,
          "load noncanonical voltage config");
    static uint8_t input[SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM];
    int32_t output[SNN_HEAD_TIMESTEPS][SNN_HEAD_ACTION_DIM] = {{0}};
    CHECK(rvrt_paicore_runner_run_sample(
              &runner, input, sizeof(input), SNN_HEAD_HIDDEN_DIM, output,
              sizeof(output),
              SNN_HEAD_ACTION_DIM * sizeof(int32_t)) == RVRT_SESSION_OK,
          "generic exact decoder accepts offset voltage addresses");
    CHECK(output[0][0] == 1 && output[7][6] == 14,
          "generic exact decoder maps offset addresses to elements");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release noncanonical voltage runner");
    host_mock_set_generator(NULL, NULL);
    return 0;
}

static int verify_nonzero_thread_identity(void)
{
    static uint8_t artifact[4096U] __attribute__((aligned(16)));
    unsigned artifact_size = 0U;
    CHECK(host_build_data_artifact(artifact, sizeof(artifact), &artifact_size,
                                   5U, 32U, 0U, 1U) == 0,
          "build nonzero-thread exact artifact");
    static rvrt_frame_t frame_buffer[8U];
    static uint32_t coverage[1U];
    const rvrt_paicore_runner_prepare_config_t prepare = {
        .artifact_data = artifact,
        .artifact_size = artifact_size,
        .timeout_ms = 100U,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_EXACT,
    };
    const rvrt_paicore_runner_attach_config_t attach = {
        .frame_buffer = frame_buffer,
        .frame_capacity = 8U,
        .coverage_bitmap = coverage,
        .coverage_word_capacity = 1U,
    };
    rvrt_paicore_runner_t runner = {0};
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare nonzero-thread runner");
    tiny_generator_t generator = {.thread_id = 5U, .output_base = 32U};
    host_mock_set_generator(tiny_generator, &generator);
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) == RVRT_SESSION_OK,
          "attach nonzero-thread runner");
    CHECK(rvrt_paicore_runner_load_config(&runner) == RVRT_SESSION_OK,
          "load nonzero-thread config");
    uint8_t input[1U] = {0U};
    uint8_t output[4U] = {0U};
    CHECK(rvrt_paicore_runner_run_sample(&runner, input, sizeof(input), 1U,
                                         output, sizeof(output),
                                         4U) == RVRT_SESSION_OK,
          "COMPLETE metadata thread_id is accepted");
    CHECK(output[0] == 0U && output[1] == 1U && output[2] == 0U &&
              output[3] == 1U,
          "nonzero-thread DATA decoded");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release nonzero-thread runner");

    runner = (rvrt_paicore_runner_t){0};
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare wrong-identity runner");
    generator.thread_id = 0U;
    host_mock_set_generator(tiny_generator, &generator);
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) == RVRT_SESSION_OK,
          "attach wrong-identity runner");
    CHECK(rvrt_paicore_runner_load_config(&runner) == RVRT_SESSION_OK,
          "load wrong-identity config");
    CHECK(rvrt_paicore_runner_run_sample(&runner, input, sizeof(input), 1U,
                                         output, sizeof(output),
                                         4U) == RVRT_SESSION_RUNTIME_ERROR,
          "array index zero cannot impersonate metadata thread_id five");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release wrong-identity runner");
    host_mock_set_generator(NULL, NULL);
    return 0;
}

static int verify_fast_nonzero_base_layout(void)
{
    static uint8_t artifact[4096U] __attribute__((aligned(16)));
    unsigned artifact_size = 0U;
    rvrt_paicore_runner_t runner = {0};
    rvrt_paicore_runner_prepare_config_t prepare = {
        .artifact_data = artifact,
        .timeout_ms = 100U,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_EXACT,
    };
    CHECK(host_build_data_artifact(artifact, sizeof(artifact), &artifact_size,
                                   5U, 32U, 0U, 1U) == 0,
          "build contiguous nonzero-base artifact");
    prepare.artifact_size = artifact_size;
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare contiguous nonzero-base artifact");
    CHECK(runner.has_fast_data_layout,
          "contiguous nonzero base selects fast DATA decoder");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release contiguous nonzero-base runner");

    CHECK(host_build_data_artifact(artifact, sizeof(artifact), &artifact_size,
                                   5U, 32U, 1U, 1U) == 0,
          "build holed output artifact");
    prepare.artifact_size = artifact_size;
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare holed output artifact");
    CHECK(!runner.has_fast_data_layout,
          "output-address hole falls back to generic decoder");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release holed output runner");

    CHECK(host_build_data_artifact(artifact, sizeof(artifact), &artifact_size,
                                   5U, 32U, 2U, 1U) == 0,
          "build duplicate-element output artifact");
    prepare.artifact_size = artifact_size;
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare duplicate-element output artifact");
    CHECK(!runner.has_fast_data_layout,
          "duplicate output element falls back to generic decoder");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release duplicate-element output runner");
    return 0;
}

static int prepare_tiny_runner(rvrt_paicore_runner_t *runner,
                               rvrt_frame_t *frame_buffer, uint32_t *coverage,
                               uint8_t *artifact, unsigned *artifact_size)
{
    if (host_build_data_artifact(artifact, 4096U, artifact_size, 5U, 32U, 0U,
                                 1U) != 0) {
        return 1;
    }
    const rvrt_paicore_runner_prepare_config_t prepare = {
        .artifact_data = artifact,
        .artifact_size = *artifact_size,
        .timeout_ms = 100U,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_EXACT,
    };
    const rvrt_paicore_runner_attach_config_t attach = {
        .frame_buffer = frame_buffer,
        .frame_capacity = 8U,
        .coverage_bitmap = coverage,
        .coverage_word_capacity = 1U,
    };
    if ((rvrt_paicore_runner_prepare(runner, &prepare) != RVRT_SESSION_OK) ||
        (rvrt_paicore_runner_attach(runner, &attach) != RVRT_SESSION_OK) ||
        (rvrt_paicore_runner_load_config(runner) != RVRT_SESSION_OK)) {
        return 1;
    }
    return 0;
}

static int verify_exact_exchange(void)
{
    static uint8_t artifact[4096U] __attribute__((aligned(16)));
    rvrt_frame_t frame_buffer[8U];
    uint32_t coverage[1U];
    unsigned artifact_size = 0U;
    rvrt_paicore_runner_t runner = {0};
    CHECK(prepare_tiny_runner(&runner, frame_buffer, coverage, artifact,
                              &artifact_size) == 0,
          "prepare exact exchange runner");
    exchange_generator_t generator = {
        .response_count = 2U,
        .response_high = UINT32_C(0xE1234567),
    };
    exchange_handler_t handler = {.expected_count = 2U};
    host_mock_set_generator(exchange_generator, &generator);
    const rvrt_frame_t request = {UINT32_C(0xF0000000), 16U};
    rvrt_session_stats_t stats_before = {0};
    rvrt_session_stats_t stats_after = {0};
    CHECK(rvrt_paicore_runner_get_stats(&runner, &stats_before) ==
              RVRT_SESSION_OK,
          "snapshot stats before raw exchange");
    CHECK(rvrt_paicore_runner_exchange_exact(&runner, &request, 2U, 100U,
                                             exchange_handler,
                                             &handler) == RVRT_SESSION_OK,
          "raw exact exchange does not classify E-prefix payloads");
    CHECK(handler.received_count == 2U,
          "raw exact exchange delivers E-prefix payloads");
    generator.response_count = 16U;
    generator.response_high = UINT32_C(0x81234567);
    handler = (exchange_handler_t){.expected_count = 16U};
    CHECK(rvrt_paicore_runner_exchange_exact(&runner, &request, 16U, 100U,
                                             exchange_handler,
                                             &handler) == RVRT_SESSION_OK,
          "exact exchange accepts handler-delimited response");
    CHECK(handler.received_count == 16U,
          "exact exchange delivers every response frame");
    generator.response_count = 129U;
    handler = (exchange_handler_t){.expected_count = 129U};
    CHECK(rvrt_paicore_runner_exchange_exact(&runner, &request, 129U, 1000U,
                                             exchange_handler,
                                             &handler) == RVRT_SESSION_OK,
          "exact exchange supports response larger than RX scratch");
    CHECK(handler.received_count == 129U,
          "exact exchange delivers 129 response frames");
    CHECK(rvrt_paicore_runner_get_stats(&runner, &stats_after) ==
              RVRT_SESSION_OK,
          "snapshot stats after raw exchange");
    CHECK(stats_after.sent_frames == stats_before.sent_frames + 3U &&
              stats_after.rx_frames == stats_before.rx_frames + 147U &&
              stats_after.rx_irq_count == stats_before.rx_irq_count + 3U &&
              stats_after.output_work_frames ==
                  stats_before.output_work_frames &&
              stats_after.complete_frames == stats_before.complete_frames,
          "raw exchange counts transport without WORK/COMPLETE classification");
    CHECK(runner.session.sync_mode == RVRT_SESSION_SYNC_MODE_UNSET &&
              runner.session.completed_timesteps == 0U,
          "exact exchange preserves sync epoch");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release exact exchange runner");

    static const uint32_t semantic_counts[] = {2U, 4U};
    for (uint32_t index = 0U; index < 2U; ++index) {
        runner = (rvrt_paicore_runner_t){0};
        CHECK(prepare_tiny_runner(&runner, frame_buffer, coverage, artifact,
                                  &artifact_size) == 0,
              "prepare semantic-boundary exchange runner");
        generator.response_count = 3U;
        handler = (exchange_handler_t){
            .expected_count = semantic_counts[index],
        };
        host_mock_set_generator(exchange_generator, &generator);
        rvrt_session_stats_t semantic_before = {0};
        rvrt_session_stats_t semantic_after = {0};
        CHECK(rvrt_paicore_runner_get_stats(&runner, &semantic_before) ==
                  RVRT_SESSION_OK,
              "snapshot stats before semantic-boundary failure");
        CHECK(rvrt_paicore_runner_exchange_exact(
                  &runner, &request, 3U, 100U, exchange_handler, &handler) ==
                  RVRT_SESSION_RUNTIME_ERROR,
              "early and missing semantic completion are rejected");
        CHECK(rvrt_paicore_runner_get_stats(&runner, &semantic_after) ==
                      RVRT_SESSION_OK &&
                  semantic_after.rx_frames == semantic_before.rx_frames + 3U &&
                  semantic_after.rx_irq_count ==
                      semantic_before.rx_irq_count + 1U,
              "semantic-boundary failure still drains the fixed goal");
        CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
              "release semantic-boundary failure runner");
    }

    runner = (rvrt_paicore_runner_t){0};
    CHECK(prepare_tiny_runner(&runner, frame_buffer, coverage, artifact,
                              &artifact_size) == 0,
          "prepare failing exchange runner");
    generator.response_count = 3U;
    handler = (exchange_handler_t){.expected_count = 3U, .fail_at = 2U};
    host_mock_set_generator(exchange_generator, &generator);
    rvrt_session_stats_t failure_stats_before = {0};
    rvrt_session_stats_t failure_stats_after = {0};
    CHECK(rvrt_paicore_runner_get_stats(&runner, &failure_stats_before) ==
              RVRT_SESSION_OK,
          "snapshot stats before handler failure");
    CHECK(rvrt_paicore_runner_exchange_exact(&runner, &request, 3U, 100U,
                                             exchange_handler, &handler) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "exchange handler error is returned");
    CHECK(rvrt_paicore_runner_get_stats(&runner, &failure_stats_after) ==
                  RVRT_SESSION_OK &&
              failure_stats_after.rx_frames ==
                  failure_stats_before.rx_frames + 3U &&
              failure_stats_after.rx_irq_count ==
                  failure_stats_before.rx_irq_count + 1U,
          "handler failure drains the fixed goal in the triggering IRQ");
    CHECK(rvrt_paicore_runner_exchange_exact(&runner, &request, 3U, 100U,
                                             exchange_handler,
                                             &handler) == RVRT_SESSION_FAULTED,
          "exchange handler error faults session");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release failed exchange runner");

    runner = (rvrt_paicore_runner_t){0};
    CHECK(prepare_tiny_runner(&runner, frame_buffer, coverage, artifact,
                              &artifact_size) == 0,
          "prepare timeout exchange runner");
    generator.response_count = 1U;
    handler = (exchange_handler_t){.expected_count = 2U};
    host_mock_set_generator(exchange_generator, &generator);
    CHECK(rvrt_paicore_runner_exchange_exact(&runner, &request, 2U, 2U,
                                             exchange_handler,
                                             &handler) ==
              RVRT_SESSION_HARDWARE_ERROR,
          "host mock represents a missing exact frame as a failed MMIO read");
    CHECK(rvrt_paicore_runner_exchange_exact(&runner, &request, 2U, 2U,
                                             exchange_handler,
                                             &handler) == RVRT_SESSION_FAULTED,
          "missing-frame failure faults session");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release timed-out exchange runner");
    host_mock_set_generator(NULL, NULL);
    return 0;
}

static int verify_exact_fault(uint32_t fault,
                              rvrt_session_status_t expected_status)
{
    rvrt_paicore_runner_prepare_config_t prepare;
    rvrt_paicore_runner_attach_config_t attach;
    make_fc3_configs(&prepare, &attach);
    prepare.timeout_ms = 2U;
    rvrt_paicore_runner_t runner = {0};
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare fault runner");
    exact_generator_t generator = {
        .target_lcn = runner.output_view.target_lcn,
        .fault = fault,
    };
    uint32_t output_count = 0U;
    CHECK(host_enum_output_axons(snn_head_fc3_artifact_start, 85088U,
                                 generator.axons, SNN_HEAD_ACTION_DIM,
                                 &output_count) == 0 &&
              output_count == SNN_HEAD_ACTION_DIM,
          "enumerate fault output axons");
    host_mock_set_generator(exact_generator, &generator);
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) == RVRT_SESSION_OK,
          "attach fault runner");
    CHECK(rvrt_paicore_runner_load_config(&runner) == RVRT_SESSION_OK,
          "load fault config");
    static uint8_t input[SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM];
    int32_t output[SNN_HEAD_TIMESTEPS][SNN_HEAD_ACTION_DIM];
    CHECK(rvrt_paicore_runner_run_sample(
              &runner, input, sizeof(input), SNN_HEAD_HIDDEN_DIM, output,
              sizeof(output),
              SNN_HEAD_ACTION_DIM * sizeof(int32_t)) == expected_status,
          "exact integrity fault has precise status");
    CHECK(rvrt_paicore_runner_run_sample(
              &runner, input, sizeof(input), SNN_HEAD_HIDDEN_DIM, output,
              sizeof(output),
              SNN_HEAD_ACTION_DIM * sizeof(int32_t)) == RVRT_SESSION_FAULTED,
          "integrity failure faults session and stops further work");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release faulted runner");
    host_mock_set_generator(NULL, NULL);
    return 0;
}

int main(void)
{
    if (snn_head_host_load_artifacts(SNN_HEAD_ASSET_DIR) != 0) {
        return 2;
    }
    if ((verify_prepared_lifecycle() != 0) ||
        (verify_invalid_transitions() != 0) ||
        (verify_exact_rejects_delayed_schedule() != 0) ||
        (verify_exact_complete_before_voltage() != 0) ||
        (verify_exact_zero_data() != 0) ||
        (verify_exact_noncanonical_voltage_mapping() != 0) ||
        (verify_nonzero_thread_identity() != 0) ||
        (verify_exact_exchange() != 0) ||
        (verify_fast_nonzero_base_layout() != 0) ||
        (verify_exact_fault(EXACT_FAULT_MISSING,
                            RVRT_SESSION_HARDWARE_ERROR) != 0) ||
        (verify_exact_fault(EXACT_FAULT_DUPLICATE_LANE,
                            RVRT_SESSION_RUNTIME_ERROR) != 0) ||
        (verify_exact_fault(EXACT_FAULT_DUPLICATE_COMPLETE,
                            RVRT_SESSION_RUNTIME_ERROR) != 0) ||
        (verify_exact_fault(EXACT_FAULT_WRONG_THREAD,
                            RVRT_SESSION_RUNTIME_ERROR) != 0) ||
        (verify_exact_fault(EXACT_FAULT_WRONG_KIND,
                            RVRT_SESSION_RUNTIME_ERROR) != 0) ||
        (verify_exact_fault(EXACT_FAULT_WRONG_ADDRESS,
                            RVRT_SESSION_RUNTIME_ERROR) != 0) ||
        (verify_exact_fault(EXACT_FAULT_WRONG_TIME,
                            RVRT_SESSION_RUNTIME_ERROR) != 0) ||
        (verify_exact_fault(EXACT_FAULT_INIT_OUTPUT,
                            RVRT_SESSION_RUNTIME_ERROR) != 0)) {
        return 1;
    }
    puts("PREPARED RUNNER HOST TEST: PASS");
    return 0;
}
