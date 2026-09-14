#include "support.h"
#include <string.h>

#define COMPLETE UINT32_C(0xE0000000)
enum {
    GOOD,
    MISSING,
    DUPLICATE,
    DUPLICATE_COMPLETE,
    WRONG_THREAD,
    WRONG_KIND,
    WRONG_ADDRESS,
    WRONG_TIME,
    INIT_OUTPUT,
    INIT_WRONG_THREAD
};
typedef struct generator_s {
    bool voltage;
    uint32_t layout;
    uint32_t fault;
    uint32_t target;
    uint32_t target_lcn;
} generator_t;
static uint32_t generator(void *user, uint32_t high, uint32_t low,
                          rvrt_frame_t *out, uint32_t capacity)
{
    generator_t *state = user;
    (void)capacity;
    if ((high >> 28U) == 0xDU) {
        state->target = 0U;
        out[0] = (rvrt_frame_t){COMPLETE, 5U};
        if (state->fault == INIT_OUTPUT)
            out[0].high = UINT32_C(0x80000000);
        if (state->fault == INIT_WRONG_THREAD)
            out[0].low = 0U;
        return 1U;
    }
    if ((high >> 28U) != 0xCU || low != 1U)
        return 0U;
    const uint32_t timestep = state->target++;
    const uint32_t lanes = state->voltage ? 4U : 1U;
    uint32_t n = 0U;
    /* Alternate COMPLETE first/last and reverse element/lane arrival order. */
    if (timestep == 0U)
        out[n++] = (rvrt_frame_t){COMPLETE, 5U};
    for (uint32_t elem = 4U; elem > 0U; --elem) {
        uint32_t axon = 32U + elem - 1U;
        if (state->layout == 1U && elem > 1U)
            ++axon;
        for (uint32_t lane = lanes; lane > 0U; --lane) {
            const uint32_t value =
                state->voltage ? UINT32_C(0x87654321) + timestep + elem : 0U;
            out[n++] = (rvrt_frame_t){
                state->voltage ? UINT32_C(0xA0000000) : UINT32_C(0x80000000),
                ((timestep << state->target_lcn) << 17U) |
                    ((axon + (lane - 1U) * 8U) << 8U) |
                    ((value >> ((lane - 1U) * 8U)) & 0xFFU),
            };
        }
    }
    if (timestep != 0U)
        out[n++] = (rvrt_frame_t){COMPLETE, 5U};
    if (timestep == 0U) {
        switch (state->fault) {
            case MISSING:
                --n;
                break;
            case DUPLICATE:
                out[2] = out[1];
                break;
            case DUPLICATE_COMPLETE:
                out[1] = out[0];
                break;
            case WRONG_THREAD:
                out[0].low = 0U;
                break;
            case WRONG_KIND:
                out[1].high ^= UINT32_C(1) << 29U;
                break;
            case WRONG_ADDRESS:
                out[1].low = (out[1].low & ~UINT32_C(0x1FF00)) | (511U << 8U);
                break;
            case WRONG_TIME:
                out[1].low |= (3U << state->target_lcn) << 17U;
                break;
            default:
                break;
        }
    }
    return n;
}
static int sample(bool voltage, uint32_t layout, uint32_t fault)
{
    uint8_t artifact[4096] __attribute__((aligned(16)));
    unsigned size;
    CHECK(host_build_artifact(artifact, sizeof(artifact), &size, 5U, 32U,
                              layout, 1U, voltage, 2U) == 0,
          "build exact fixture");
    rvrt_voltage_decode_state_t voltage_state[8];
    rvrt_paicore_runner_prepare_config_t prepare = {
        .artifact_data = artifact,
        .artifact_size = size,
        .voltage_state = voltage_state,
        .voltage_state_capacity = 8U,
        .timeout_ms = 100U,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_EXACT,
    };
    rvrt_frame_t frames[2];
    uint32_t coverage[1];
    const rvrt_paicore_runner_attach_config_t attach = {
        .frame_buffer = frames,
        .frame_capacity = 2U,
        .coverage_bitmap = coverage,
        .coverage_word_capacity = 1U,
    };
    rvrt_paicore_runner_t runner = {0};
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare exact runner");
    if (layout)
        CHECK(!runner.has_fast_data_layout && !runner.has_fast_voltage_layout,
              "generic mapping uses fallback");
    generator_t state = {.voltage = voltage,
                         .layout = layout,
                         .fault = fault,
                         .target_lcn = runner.output_view.target_lcn};
    host_mock_set_generator(generator, &state);
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) == RVRT_SESSION_OK,
          "attach exact runner");
    CHECK(rvrt_paicore_runner_load_config(&runner) == RVRT_SESSION_OK,
          "load exact runner");
    const uint8_t input[2] = {0};
    int32_t output[8];
    const size_t row = voltage ? 4U * sizeof(int32_t) : 4U;
    const rvrt_session_status_t expected = fault == GOOD ? RVRT_SESSION_OK
                                           : fault == MISSING
                                               ? RVRT_SESSION_HARDWARE_ERROR
                                               : RVRT_SESSION_RUNTIME_ERROR;
    const rvrt_session_status_t status = rvrt_paicore_runner_run_sample(
        &runner, input, sizeof(input), 1U, output, sizeof(output), row);
    if (status != expected) {
        fprintf(
            stderr,
            "voltage=%u layout=%u fault=%u target=%u status=%s expected=%s\n",
            voltage, layout, fault, state.target,
            rvrt_session_status_string(status),
            rvrt_session_status_string(expected));
    }
    CHECK(status == expected, "exact coverage and identity status");
    if (fault == GOOD) {
        if (voltage) {
            for (uint32_t t = 0; t < 2U; ++t)
                for (uint32_t e = 0; e < 4U; ++e)
                    CHECK((uint32_t)output[t * 4U + e] ==
                              UINT32_C(0x87654321) + t + e + 1U,
                          "arbitrary lane order reconstructs every signed "
                          "voltage");
        } else {
            for (uint32_t i = 0; i < 8U; ++i)
                CHECK(((uint8_t *)output)[i] == 0U,
                      "explicit zero DATA satisfies coverage");
        }
        rvrt_session_stats_t stats = {0};
        CHECK(rvrt_paicore_runner_get_stats(&runner, &stats) == RVRT_SESSION_OK,
              "read exact stats");
#if RVRT_ENABLE_STATS
        CHECK(stats.rx_irq_count == 3U,
              "each known response drains in one IRQ");
#endif
        CHECK(rvrt_paicore_runner_run_sample(&runner, input, sizeof(input), 1U,
                                             output, sizeof(output),
                                             row) == RVRT_SESSION_OK,
              "repeated sample clears coverage and INIT resets time");
    } else {
        CHECK(rvrt_paicore_runner_run_sample(&runner, input, sizeof(input), 1U,
                                             output, sizeof(output),
                                             row) == RVRT_SESSION_FAULTED,
              "integrity failure prevents further inference");
    }
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release exact runner");
    host_mock_set_generator(NULL, NULL);
    return 0;
}
static int guards(void)
{
    uint8_t artifact[4096] __attribute__((aligned(16)));
    unsigned size;
    CHECK(host_build_data_artifact(artifact, sizeof(artifact), &size, 5U, 32U,
                                   0U, 2U) == 0,
          "build delayed fixture");
    rvrt_paicore_runner_prepare_config_t prepare = {
        .artifact_data = artifact,
        .artifact_size = size,
        .timeout_ms = 100U,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_EXACT,
    };
    rvrt_paicore_runner_t runner = {0};
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) ==
              RVRT_SESSION_RUNTIME_ERROR,
          "exact rejects delayed output");
    prepare.rx_policy = RVRT_PAICORE_RUNNER_RX_SPARSE;
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "sparse retains delayed schedule");
    uint32_t identity = 0U;
    CHECK(rvrt_artifact_thread_id(&runner.artifact, 0U, &identity) ==
                  RVRT_ARTIFACT_OK &&
              identity == 5U,
          "accessor returns metadata identity");
    CHECK(rvrt_artifact_thread_id(&runner.artifact, 1U, &identity) !=
              RVRT_ARTIFACT_OK,
          "accessor rejects absent thread");
    CHECK(rvrt_artifact_thread_id(&runner.artifact, 0U, NULL) !=
              RVRT_ARTIFACT_OK,
          "accessor rejects null output");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release delayed runner");
    CHECK(host_build_data_artifact(artifact, sizeof(artifact), &size, 5U, 32U,
                                   0U, 1U) == 0,
          "build attachment fixture");
    prepare.artifact_size = size;
    prepare.rx_policy = RVRT_PAICORE_RUNNER_RX_EXACT;
    CHECK(rvrt_paicore_runner_prepare(&runner, &prepare) == RVRT_SESSION_OK,
          "prepare exact attachment fixture");
    rvrt_frame_t frames[2];
    const rvrt_paicore_runner_attach_config_t attach = {.frame_buffer = frames,
                                                        .frame_capacity = 2U};
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) ==
              RVRT_SESSION_BUFFER_TOO_SMALL,
          "exact requires coverage scratch");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release coverage failure");
    return 0;
}
int main(void)
{
    CHECK(guards() == 0, "exact guards");
    for (uint32_t voltage = 0U; voltage < 2U; ++voltage) {
        CHECK(sample(voltage, 0U, GOOD) == 0, "fast exact success");
        CHECK(sample(voltage, 1U, GOOD) == 0, "generic exact success");
        for (uint32_t fault = MISSING; fault <= INIT_WRONG_THREAD; ++fault)
            CHECK(sample(voltage, 0U, fault) == 0, "exact fault matrix");
    }
    puts("RUNTIME EXACT: PASS");
    return 0;
}
