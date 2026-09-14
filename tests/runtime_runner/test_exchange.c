#include "paicore_runner_internal.h"
#include "support.h"

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
#if RVRT_ENABLE_STATS
    CHECK(stats_after.sent_frames == stats_before.sent_frames + 3U &&
              stats_after.rx_frames == stats_before.rx_frames + 147U &&
              stats_after.rx_irq_count == stats_before.rx_irq_count + 3U &&
              stats_after.output_work_frames ==
                  stats_before.output_work_frames &&
              stats_after.complete_frames == stats_before.complete_frames,
          "raw exchange counts transport without WORK/COMPLETE classification");
#endif
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
        CHECK(rvrt_paicore_runner_exchange_exact(&runner, &request, 3U, 100U,
                                                 exchange_handler, &handler) ==
                  RVRT_SESSION_RUNTIME_ERROR,
              "early and missing semantic completion are rejected");
#if RVRT_ENABLE_STATS
        CHECK(rvrt_paicore_runner_get_stats(&runner, &semantic_after) ==
                      RVRT_SESSION_OK &&
                  semantic_after.rx_frames == semantic_before.rx_frames + 3U &&
                  semantic_after.rx_irq_count ==
                      semantic_before.rx_irq_count + 1U,
              "semantic-boundary failure still drains the fixed goal");
#endif
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
#if RVRT_ENABLE_STATS
    CHECK(rvrt_paicore_runner_get_stats(&runner, &failure_stats_after) ==
                  RVRT_SESSION_OK &&
              failure_stats_after.rx_frames ==
                  failure_stats_before.rx_frames + 3U &&
              failure_stats_after.rx_irq_count ==
                  failure_stats_before.rx_irq_count + 1U,
          "handler failure drains the fixed goal in the triggering IRQ");
#endif
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
                                             exchange_handler, &handler) ==
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

int main(void)
{
    CHECK(verify_exact_exchange() == 0, "raw exact exchange suite");
    puts("RUNTIME EXACT EXCHANGE: PASS");
    return 0;
}
