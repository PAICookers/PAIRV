/* Low-level transport regression: production callers use
 * RuntimeSession/ThreadRunner; this target exercises the private barrier and
 * codec boundary without making it an application dependency. */
#include "artifact_reader.h"
#include "frame_codec.h"
#include "mock_runtime_hw.h"
#include "transport_io_internal.h"
#include "transport_thread_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void paicore_noc_handler(void);

#ifndef RVRT_TEST_ASSET_DIR
#define RVRT_TEST_ASSET_DIR "assets"
#endif

#define TEST_ALIGNMENT 8U
#define TEST_PATH_BYTES 512U
#define TEST_DATA_HIGH 0x80000000U
#define TEST_COMPLETE_HIGH 0xE0000000U

typedef struct test_artifact_s {
    uint8_t *data;
    size_t size;
    rvrt_artifact_view_t artifact;
} test_artifact_t;

static rvrt_frame_t work_frame(uint32_t high, uint32_t axon_bit_idx,
                               uint32_t timestamp, uint8_t payload)
{
    return (rvrt_frame_t){high | (((timestamp >> 7U) & 0x1U) << 28U),
                          ((timestamp & 0x7FU) << 17U) | (axon_bit_idx << 8U) |
                              (uint32_t)payload};
}

static rvrt_frame_t data_frame(uint32_t axon_bit_idx, uint8_t payload)
{
    return work_frame(TEST_DATA_HIGH, axon_bit_idx, 0U, payload);
}

static rvrt_frame_t complete_frame(void)
{
    return (rvrt_frame_t){TEST_COMPLETE_HIGH, 0U};
}

static rvrt_runtime_status_t handle_ignored_frame(void *user_data,
                                                  const rvrt_frame_t *frame)
{
    (void)user_data;
    (void)frame;
    return RVRT_RUNTIME_OK;
}

static rvrt_runtime_status_t reject_frame_handler(void *user_data,
                                                  const rvrt_frame_t *frame)
{
    (void)user_data;
    (void)frame;
    return RVRT_RUNTIME_RUNTIME_ERROR;
}

static int read_artifact_path(const char *path, test_artifact_t *out)
{
    if ((path == NULL) || (out == NULL)) {
        return 1;
    }

    FILE *const file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return 1;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return 1;
    }
    const long file_size = ftell(file);
    if (file_size <= 0L) {
        fclose(file);
        return 1;
    }
    rewind(file);

    const size_t storage_size = ((size_t)file_size + (TEST_ALIGNMENT - 1U)) &
                                ~(size_t)(TEST_ALIGNMENT - 1U);
    uint8_t *const data = aligned_alloc(TEST_ALIGNMENT, storage_size);
    if ((data == NULL) ||
        (fread(data, 1U, (size_t)file_size, file) != (size_t)file_size)) {
        free(data);
        fclose(file);
        return 1;
    }
    fclose(file);

    out->data = data;
    out->size = (size_t)file_size;
    if (rvrt_artifact_read(data, (size_t)file_size, &out->artifact) !=
        RVRT_ARTIFACT_OK) {
        free(data);
        out->data = NULL;
        return 1;
    }
    return 0;
}

static int read_artifact(const char *name, test_artifact_t *out)
{
    char path[TEST_PATH_BYTES];
    const int length =
        snprintf(path, sizeof(path), "%s/%s", RVRT_TEST_ASSET_DIR, name);
    if ((length < 0) || ((size_t)length >= sizeof(path))) {
        return 1;
    }
    return read_artifact_path(path, out);
}

static void free_artifact(test_artifact_t *artifact)
{
    free(artifact->data);
    artifact->data = NULL;
    artifact->size = 0U;
}

static int expect_session(rvrt_runtime_status_t actual,
                          rvrt_runtime_status_t expected, const char *stage)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s status=%s expected=%s\n", stage,
            rvrt_transport_status_string(actual),
            rvrt_transport_status_string(expected));
    return 1;
}

static int init_session(const rvrt_artifact_view_t *artifact,
                        rvrt_transport_t *session, rvrt_frame_t *rx_frames,
                        uint32_t rx_capacity)
{
    const rvrt_transport_config_t config = {
        artifact,
        rx_frames,
        rx_capacity,
    };
    return expect_session(rvrt_transport_init(session, &config),
                          RVRT_RUNTIME_OK, "session init");
}

static int verify_manual_flow(void)
{
    test_artifact_t file = {0};
    if (read_artifact("compile_artifacts_manual.bin", &file) != 0) {
        return 1;
    }

    int result = 1;
    mock_runtime_reset();
    rvrt_frame_t rx_storage[8];
    rvrt_transport_t session = {0};
    rvrt_runtime_sync_mode_t sync_mode = RVRT_RUNTIME_SYNC_MODE_UNSET;
    if ((init_session(&file.artifact, &session, rx_storage, 8U) != 0) ||
        (expect_session(rvrt_transport_load_config(&session), RVRT_RUNTIME_OK,
                        "load config") != 0)) {
        goto cleanup;
    }

    rvrt_artifact_input_mapping_view_t input_view = {0};
    rvrt_artifact_output_mapping_view_t output_view = {0};
    if ((rvrt_artifact_get_input_mapping_view(
             &file.artifact, 0U, 0U, &input_view) != RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_output_mapping_view(
             &file.artifact, 0U, 0U, &output_view) != RVRT_ARTIFACT_OK)) {
        goto cleanup;
    }

    const uint8_t input[] = {3U, 4U};
    rvrt_frame_t input_workspace[1];
    if ((rvrt_transport_send_input_timestep(&session, &input_view, 0U, input,
                                            sizeof(input), input_workspace,
                                            1U) != RVRT_RUNTIME_OK) ||
        (rvrt_transport_send_input_timestep(&session, &input_view, 1U, input,
                                            sizeof(input), input_workspace,
                                            1U) != RVRT_RUNTIME_OK) ||
        (mock_runtime_sent_count() != 5U)) {
        goto cleanup;
    }

    const rvrt_frame_t received[] = {
        data_frame(0U, 0x11U),
        data_frame(1U, 0x22U),
        complete_frame(),
    };
    mock_runtime_queue_rx(received, 3U);
    mock_runtime_probe_rx_barrier_active(&session);
    const rvrt_frame_t *raw_frames = NULL;
    uint32_t raw_count = 0U;
    if ((expect_session(
             rvrt_transport_sync_wait_payload_for_thread(
                 &session, 0U, &sync_mode, 2U, 10U, &raw_frames, &raw_count),
             RVRT_RUNTIME_OK, "manual barrier") != 0) ||
        (raw_frames != rx_storage) || (raw_count != 3U) ||
        !rvrt_frame_is_complete(&raw_frames[2]) ||
        (mock_runtime_sent_count() != 6U) ||
        ((mock_runtime_sent_frames()[5].low & 0xFFFFFFU) != 2U) ||
        (mock_runtime_nested_send_status() != RVRT_RUNTIME_RUNTIME_ERROR) ||
        (mock_runtime_nested_sync_status() != RVRT_RUNTIME_RUNTIME_ERROR)) {
        goto cleanup;
    }

    uint8_t output[2] = {0U, 0U};
    for (uint32_t i = 0U; i < raw_count; ++i) {
        bool written = false;
        if (rvrt_decode_output_frame(&output_view, &raw_frames[i], output,
                                     sizeof(output),
                                     &written) != RVRT_CODEC_STATUS_OK) {
            goto cleanup;
        }
    }
    if ((output[0] != 0x11U) || (output[1] != 0x22U)) {
        goto cleanup;
    }

    result = 0;
cleanup:
    (void)rvrt_transport_deinit(&session);
    free_artifact(&file);
    return result;
}

static int verify_model_reset(void)
{
    test_artifact_t file = {0};
    if (read_artifact("compile_artifacts_manual.bin", &file) != 0) {
        return 1;
    }

    int result = 1;
    rvrt_frame_t rx_storage[2];
    rvrt_transport_t session = {0};
    rvrt_runtime_sync_mode_t sync_mode = RVRT_RUNTIME_SYNC_MODE_UNSET;
    uint32_t completed_timesteps = 0U;
    rvrt_frame_t expected_init = {0};
    rvrt_frame_t expected_sync = {0};
    const rvrt_frame_t complete = complete_frame();

    mock_runtime_reset();
    if ((init_session(&file.artifact, &session, rx_storage, 2U) != 0) ||
        (rvrt_transport_load_config(&session) != RVRT_RUNTIME_OK) ||
        (rvrt_build_init_frame(&file.artifact, 0U, &expected_init) !=
         RVRT_CODEC_STATUS_OK) ||
        (rvrt_build_sync_payload_frame(
             &file.artifact, 0U, 2U, &expected_sync) != RVRT_CODEC_STATUS_OK)) {
        goto cleanup;
    }

    const uint32_t sent_before_reset = mock_runtime_sent_count();
    mock_runtime_queue_rx(&complete, 1U);
    if ((expect_session(
             rvrt_transport_reset_model_for_thread(
                 &session, 0U, 10U, &sync_mode, &completed_timesteps),
             RVRT_RUNTIME_OK, "model reset") != 0) ||
        (mock_runtime_sent_count() != sent_before_reset + 1U) ||
        (memcmp(&mock_runtime_sent_frames()[sent_before_reset], &expected_init,
                sizeof(expected_init)) != 0) ||
        session.rx_barrier.active) {
        goto cleanup;
    }

    mock_runtime_queue_rx(&complete, 1U);
    const rvrt_frame_t *raw_frames = NULL;
    uint32_t raw_count = 0U;
    if ((expect_session(
             rvrt_transport_sync_wait_payload_for_thread(
                 &session, 0U, &sync_mode, 2U, 10U, &raw_frames, &raw_count),
             RVRT_RUNTIME_OK, "sync after reset") != 0) ||
        (raw_count != 1U) ||
        (memcmp(&mock_runtime_sent_frames()[sent_before_reset + 1U],
                &expected_sync, sizeof(expected_sync)) != 0)) {
        goto cleanup;
    }

#if RVRT_ENABLE_STATS
    rvrt_runtime_stats_t stats_before_spurious = {0};
    rvrt_runtime_stats_t stats_after_spurious = {0};
    if ((rvrt_transport_get_stats(&session, &stats_before_spurious) !=
         RVRT_RUNTIME_OK)) {
        goto cleanup;
    }
    paicore_noc_handler();
    if ((rvrt_transport_get_stats(&session, &stats_after_spurious) !=
         RVRT_RUNTIME_OK) ||
        (stats_after_spurious.rx_irq_count !=
         stats_before_spurious.rx_irq_count) ||
        (stats_after_spurious.rx_irq_service_cycles !=
         stats_before_spurious.rx_irq_service_cycles)) {
        goto cleanup;
    }
#endif

    mock_runtime_queue_rx(NULL, 0U);
    if (expect_session(rvrt_transport_reset_model_for_thread(
                           &session, 0U, 10U, &sync_mode, &completed_timesteps),
                       RVRT_RUNTIME_TIMEOUT, "reset without response") != 0) {
        goto cleanup;
    }
    if (expect_session(rvrt_transport_reset_model_for_thread(
                           &session, 0U, 1U, &sync_mode, &completed_timesteps),
                       RVRT_RUNTIME_FAULTED,
                       "reset after missing response") != 0) {
        goto cleanup;
    }

    (void)rvrt_transport_deinit(&session);
    mock_runtime_reset();
    memset(&session, 0, sizeof(session));
    if (init_session(&file.artifact, &session, rx_storage, 2U) != 0) {
        goto cleanup;
    }
    mock_runtime_set_auto_irq(false);
    if (expect_session(rvrt_transport_reset_model_for_thread(
                           &session, 0U, 1U, &sync_mode, &completed_timesteps),
                       RVRT_RUNTIME_TIMEOUT, "reset timeout") != 0) {
        goto cleanup;
    }

    (void)rvrt_transport_deinit(&session);
    mock_runtime_reset();
    memset(&session, 0, sizeof(session));
    if (init_session(&file.artifact, &session, rx_storage, 2U) != 0) {
        goto cleanup;
    }
    mock_runtime_set_auto_irq(false);
    mock_runtime_queue_rx(&complete, 1U);
    mock_runtime_set_irq_on_disable(true);
    if (expect_session(rvrt_transport_reset_model_for_thread(
                           &session, 0U, 1U, &sync_mode, &completed_timesteps),
                       RVRT_RUNTIME_OK, "reset completion at timeout") != 0) {
        goto cleanup;
    }
    mock_runtime_set_auto_irq(true);

    rvrt_frame_t reset_storage[1] = {{0x12345678U, 0x9ABCDEF0U}};
    rvrt_transport_t reset_session = {0};
    const rvrt_frame_t reset_rx[] = {
        data_frame(0U, 0x11U),
        data_frame(1U, 0x22U),
        complete_frame(),
    };
    (void)rvrt_transport_deinit(&session);
    if (init_session(&file.artifact, &reset_session, reset_storage, 1U) != 0) {
        goto cleanup;
    }
    mock_runtime_queue_rx(reset_rx, 3U);
    if ((expect_session(
             rvrt_transport_reset_model_for_thread(
                 &reset_session, 0U, 10U, &sync_mode, &completed_timesteps),
             RVRT_RUNTIME_OK, "reset discards work") != 0) ||
        (reset_session.rx_barrier.rx_count != 0U) ||
#if RVRT_ENABLE_STATS
        (reset_session.rx_barrier.received_count != 3U) ||
        (reset_session.rx_barrier.output_work_count != 2U) ||
        (reset_session.rx_barrier.complete_count != 1U) ||
#endif
        reset_session.rx_barrier.overflow ||
        (reset_storage[0].high != 0x12345678U) ||
        (reset_storage[0].low != 0x9ABCDEF0U)) {
        goto cleanup;
    }

    reset_session.rx_barrier.active = true;
    if ((rvrt_transport_reset_model_for_thread(NULL, 0U, 1U, &sync_mode,
                                               &completed_timesteps) !=
         RVRT_RUNTIME_RUNTIME_ERROR) ||
        (rvrt_transport_reset_model_for_thread(
             &reset_session, 0U, 1U, &sync_mode, &completed_timesteps) !=
         RVRT_RUNTIME_RUNTIME_ERROR)) {
        goto cleanup;
    }
    reset_session.rx_barrier.active = false;

    result = 0;
cleanup:
    session.rx_barrier.active = false;
    reset_session.rx_barrier.active = false;
    (void)rvrt_transport_deinit(&reset_session);
    (void)rvrt_transport_deinit(&session);
    free_artifact(&file);
    return result;
}

static int verify_input_send_api(void)
{
    test_artifact_t file = {0};
    if (read_artifact("compile_artifacts_manual.bin", &file) != 0) {
        return 1;
    }

    int result = 1;
    rvrt_frame_t rx_storage[1];
    rvrt_transport_t session = {0};
    rvrt_artifact_input_mapping_view_t input_view = {0};
    const uint8_t input[] = {3U, 4U};
    const uint8_t zero_input[] = {0U, 0U};
    rvrt_frame_t one_frame_workspace[1];
    /* Exercise a caller-owned chunk larger than the former 512-frame cap. */
    rvrt_frame_t large_workspace[513U];
    const uint32_t large_workspace_capacity =
        (uint32_t)(sizeof(large_workspace) / sizeof(large_workspace[0]));
    rvrt_frame_t expected[2];

    mock_runtime_reset();
    if ((init_session(&file.artifact, &session, rx_storage, 1U) != 0) ||
        (rvrt_artifact_get_input_mapping_view(
             &file.artifact, 0U, 0U, &input_view) != RVRT_ARTIFACT_OK) ||
        (rvrt_transport_send_input_timestep(&session, &input_view, 7U, input,
                                            sizeof(input), one_frame_workspace,
                                            1U) != RVRT_RUNTIME_OK) ||
        (mock_runtime_sent_count() != 2U)) {
        goto cleanup;
    }
    memcpy(expected, mock_runtime_sent_frames(), sizeof(expected));

    (void)rvrt_transport_deinit(&session);
    mock_runtime_reset();
    memset(&session, 0, sizeof(session));
    if ((init_session(&file.artifact, &session, rx_storage, 1U) != 0) ||
        (rvrt_transport_send_input_timestep(
             &session, &input_view, 7U, input, sizeof(input), large_workspace,
             large_workspace_capacity) != RVRT_RUNTIME_OK) ||
        (mock_runtime_sent_count() != 2U) ||
        (memcmp(mock_runtime_sent_frames(), expected, sizeof(expected)) != 0) ||
        (rvrt_transport_send_input_timestep(
             &session, &input_view, 8U, zero_input, sizeof(zero_input),
             large_workspace, large_workspace_capacity) != RVRT_RUNTIME_OK) ||
        (mock_runtime_sent_count() != 2U)) {
        goto cleanup;
    }

    if ((rvrt_transport_send_input_timestep(
             &session, &input_view, 0U, input, sizeof(input), large_workspace,
             0U) != RVRT_RUNTIME_RUNTIME_ERROR) ||
        (rvrt_transport_send_input_timestep(
             &session, &input_view, 0U, input, 0U, large_workspace,
             large_workspace_capacity) != RVRT_RUNTIME_RUNTIME_ERROR) ||
        (rvrt_transport_send_input_timestep(
             &session, NULL, 0U, input, sizeof(input), large_workspace,
             large_workspace_capacity) != RVRT_RUNTIME_RUNTIME_ERROR) ||
        (rvrt_transport_send_input_timestep(
             &session, &input_view, 0U, NULL, sizeof(input), large_workspace,
             large_workspace_capacity) != RVRT_RUNTIME_RUNTIME_ERROR) ||
        (rvrt_transport_send_input_timestep(
             &session, &input_view, 0U, input, sizeof(input), NULL,
             large_workspace_capacity) != RVRT_RUNTIME_RUNTIME_ERROR) ||
        (rvrt_transport_send_input_timestep(
             NULL, &input_view, 0U, input, sizeof(input), large_workspace,
             large_workspace_capacity) != RVRT_RUNTIME_RUNTIME_ERROR)) {
        goto cleanup;
    }

    session.rx_barrier.active = true;
    if (rvrt_transport_send_input_timestep(
            &session, &input_view, 0U, input, sizeof(input), large_workspace,
            large_workspace_capacity) != RVRT_RUNTIME_RUNTIME_ERROR ||
        (mock_runtime_sent_count() != 2U)) {
        goto cleanup;
    }
    session.rx_barrier.active = false;

    result = 0;
cleanup:
    session.rx_barrier.active = false;
    (void)rvrt_transport_deinit(&session);
    free_artifact(&file);
    return result;
}

static int verify_session_lifecycle(void)
{
    test_artifact_t file = {0};
    if (read_artifact("compile_artifacts_manual.bin", &file) != 0) {
        return 1;
    }

    int result = 1;
    rvrt_frame_t first_rx[1];
    rvrt_frame_t second_rx[1];
    rvrt_transport_t first = {0};
    rvrt_transport_t second = {0};
    const rvrt_transport_config_t second_config = {
        .artifact = &file.artifact,
        .rx_frames = second_rx,
        .rx_capacity = 1U,
    };

    mock_runtime_reset();
    if ((init_session(&file.artifact, &first, first_rx, 1U) != 0) ||
        (rvrt_transport_init(&second, &second_config) != RVRT_RUNTIME_BUSY) ||
        (rvrt_transport_init(&first, &second_config) != RVRT_RUNTIME_BUSY) ||
        (rvrt_transport_deinit(NULL) != RVRT_RUNTIME_RUNTIME_ERROR) ||
        (rvrt_transport_deinit(&second) != RVRT_RUNTIME_BUSY) ||
        (rvrt_transport_deinit(&first) != RVRT_RUNTIME_OK) ||
        (rvrt_transport_deinit(&first) != RVRT_RUNTIME_OK) ||
        (rvrt_transport_init(&second, &second_config) != RVRT_RUNTIME_OK) ||
        (rvrt_transport_status_string(RVRT_RUNTIME_BUSY) == NULL)) {
        goto cleanup;
    }

    second.rx_barrier.active = true;
    if (rvrt_transport_deinit(&second) != RVRT_RUNTIME_BUSY) {
        goto cleanup;
    }
    second.rx_barrier.active = false;
    result = 0;

cleanup:
    first.rx_barrier.active = false;
    second.rx_barrier.active = false;
    (void)rvrt_transport_deinit(&second);
    (void)rvrt_transport_deinit(&first);
    free_artifact(&file);
    return result;
}

static int verify_sync_modes(void)
{
    test_artifact_t file = {0};
    if (read_artifact("compile_artifacts_manual.bin", &file) != 0) {
        return 1;
    }

    int result = 1;
    rvrt_frame_t rx_storage[1];
    rvrt_transport_t session = {0};
    rvrt_runtime_sync_mode_t sync_mode = RVRT_RUNTIME_SYNC_MODE_UNSET;
    uint32_t completed_timesteps = 0U;
    const rvrt_frame_t complete = complete_frame();
    const rvrt_frame_t *frames = NULL;
    uint32_t frame_count = 0U;

    mock_runtime_reset();
    if (init_session(&file.artifact, &session, rx_storage, 1U) != 0) {
        goto cleanup;
    }

    mock_runtime_queue_rx(&complete, 1U);
    if (expect_session(rvrt_transport_reset_model_for_thread(
                           &session, 0U, 10U, &sync_mode, &completed_timesteps),
                       RVRT_RUNTIME_OK, "sync mode reset raw") != 0) {
        goto cleanup;
    }
    mock_runtime_queue_rx(&complete, 1U);
    if ((expect_session(rvrt_transport_sync_wait_payload_for_thread(
                            &session, 0U, &sync_mode, 0x12345U, 10U, &frames,
                            &frame_count),
                        RVRT_RUNTIME_OK, "raw payload") != 0) ||
        ((mock_runtime_sent_frames()[mock_runtime_sent_count() - 1U].low &
          0xFFFFFFU) != 0x12345U) ||
        (rvrt_transport_sync_wait_until_for_thread(
             &session, 0U, 1U, 10U, &sync_mode, &completed_timesteps, &frames,
             &frame_count, NULL, NULL) != RVRT_RUNTIME_SYNC_MODE_ERROR)) {
        goto cleanup;
    }

    mock_runtime_queue_rx(&complete, 1U);
    if (expect_session(rvrt_transport_reset_model_for_thread(
                           &session, 0U, 10U, &sync_mode, &completed_timesteps),
                       RVRT_RUNTIME_OK, "sync mode reset timeline") != 0) {
        goto cleanup;
    }
    mock_runtime_queue_rx(&complete, 1U);
    if ((expect_session(rvrt_transport_sync_wait_until_for_thread(
                            &session, 0U, 0U, 10U, &sync_mode,
                            &completed_timesteps, &frames, &frame_count, NULL,
                            NULL),
                        RVRT_RUNTIME_OK, "timeline target 0") != 0) ||
        ((mock_runtime_sent_frames()[mock_runtime_sent_count() - 1U].low &
          0xFFFFFFU) != 0U) ||
        (rvrt_transport_sync_wait_until_for_thread(
             &session, 0U, 0U, 10U, &sync_mode, &completed_timesteps, &frames,
             &frame_count, NULL, NULL) != RVRT_RUNTIME_SYNC_MODE_ERROR) ||
        (rvrt_transport_sync_wait_payload_for_thread(
             &session, 0U, &sync_mode, 2U, 10U, &frames, &frame_count) !=
         RVRT_RUNTIME_SYNC_MODE_ERROR)) {
        goto cleanup;
    }
    mock_runtime_queue_rx(&complete, 1U);
    if ((expect_session(rvrt_transport_sync_wait_until_for_thread(
                            &session, 0U, 1U, 10U, &sync_mode,
                            &completed_timesteps, &frames, &frame_count, NULL,
                            NULL),
                        RVRT_RUNTIME_OK, "timeline target 1") != 0) ||
        ((mock_runtime_sent_frames()[mock_runtime_sent_count() - 1U].low &
          0xFFFFFFU) != 1U)) {
        goto cleanup;
    }
    mock_runtime_queue_rx(&complete, 1U);
    if ((expect_session(rvrt_transport_sync_wait_until_for_thread(
                            &session, 0U, 3U, 10U, &sync_mode,
                            &completed_timesteps, &frames, &frame_count, NULL,
                            NULL),
                        RVRT_RUNTIME_OK, "timeline target 3") != 0) ||
        ((mock_runtime_sent_frames()[mock_runtime_sent_count() - 1U].low &
          0xFFFFFFU) != 2U)) {
        goto cleanup;
    }
    mock_runtime_queue_rx(&complete, 1U);
    if ((expect_session(rvrt_transport_sync_wait_until_for_thread(
                            &session, 0U, 5U, 10U, &sync_mode,
                            &completed_timesteps, NULL, NULL,
                            handle_ignored_frame, NULL),
                        RVRT_RUNTIME_OK, "timeline handler target 5") != 0) ||
        ((mock_runtime_sent_frames()[mock_runtime_sent_count() - 1U].low &
          0xFFFFFFU) != 2U)) {
        goto cleanup;
    }

    result = 0;
cleanup:
    (void)rvrt_transport_deinit(&session);
    free_artifact(&file);
    return result;
}

static int verify_large_irq_drain(void)
{
    enum { WORK_FRAMES = 80U, RX_FRAMES = WORK_FRAMES + 1U };
    test_artifact_t file = {0};
    if (read_artifact("compile_artifacts_manual.bin", &file) != 0) {
        return 1;
    }

    int result = 1;
    rvrt_frame_t rx_storage[RX_FRAMES];
    rvrt_frame_t received[RX_FRAMES];
    for (uint32_t index = 0U; index < WORK_FRAMES; ++index) {
        received[index] = data_frame(index & 1U, (uint8_t)index);
    }
    received[WORK_FRAMES] = complete_frame();

    mock_runtime_reset();
    rvrt_transport_t session = {0};
    rvrt_runtime_sync_mode_t sync_mode = RVRT_RUNTIME_SYNC_MODE_UNSET;
    if (init_session(&file.artifact, &session, rx_storage, RX_FRAMES) != 0) {
        goto cleanup;
    }
    mock_runtime_queue_rx(received, RX_FRAMES);

    const rvrt_frame_t *frames = NULL;
    uint32_t frame_count = 0U;
    if ((rvrt_transport_sync_wait_payload_for_thread(
             &session, 0U, &sync_mode, 1U, 10U, &frames, &frame_count) !=
         RVRT_RUNTIME_OK) ||
        (frames != rx_storage) || (frame_count != RX_FRAMES) ||
        !rvrt_frame_is_complete(&frames[WORK_FRAMES])) {
        goto cleanup;
    }

    result = 0;
cleanup:
    (void)rvrt_transport_deinit(&session);
    free_artifact(&file);
    return result;
}

static int verify_barrier_recovery(void)
{
    test_artifact_t file = {0};
    if (read_artifact("compile_artifacts_manual.bin", &file) != 0) {
        return 1;
    }

    int result = 1;
    rvrt_frame_t rx_storage[2];
    rvrt_transport_t session = {0};
    rvrt_runtime_sync_mode_t sync_mode = RVRT_RUNTIME_SYNC_MODE_UNSET;
    uint32_t completed_timesteps = 0U;
    const rvrt_frame_t send_frame = {0U, 0U};
    const rvrt_frame_t *raw_frames = NULL;
    uint32_t raw_count = 0U;

    mock_runtime_reset();
    if (init_session(&file.artifact, &session, rx_storage, 2U) != 0) {
        goto cleanup;
    }
    mock_runtime_set_auto_irq(false);
    if ((expect_session(
             rvrt_transport_sync_wait_payload_for_thread(
                 &session, 0U, &sync_mode, 1U, 1U, &raw_frames, &raw_count),
             RVRT_RUNTIME_TIMEOUT, "timeout") != 0) ||
        session.rx_barrier.active ||
        (expect_session(rvrt_transport_send_frames(&session, &send_frame, 1U),
                        RVRT_RUNTIME_FAULTED, "send after timeout") != 0) ||
        (expect_session(rvrt_transport_load_config(&session),
                        RVRT_RUNTIME_FAULTED, "config after timeout") != 0) ||
        (expect_session(rvrt_transport_reset_model_for_thread(
                            &session, 0U, 1U, &sync_mode, &completed_timesteps),
                        RVRT_RUNTIME_FAULTED, "reset after timeout") != 0) ||
        (expect_session(
             rvrt_transport_sync_wait_payload_for_thread(
                 &session, 0U, &sync_mode, 1U, 1U, &raw_frames, &raw_count),
             RVRT_RUNTIME_FAULTED, "sync after timeout") != 0)) {
        goto cleanup;
    }

    (void)rvrt_transport_deinit(&session);
    mock_runtime_reset();
    memset(&session, 0, sizeof(session));
    if (init_session(&file.artifact, &session, rx_storage, 2U) != 0) {
        goto cleanup;
    }
    const rvrt_frame_t overflow_frames[] = {
        data_frame(0U, 1U), data_frame(1U, 2U), complete_frame()};
    mock_runtime_queue_rx(overflow_frames, 3U);
    if ((expect_session(
             rvrt_transport_sync_wait_payload_for_thread(
                 &session, 0U, &sync_mode, 1U, 10U, &raw_frames, &raw_count),
             RVRT_RUNTIME_OVERFLOW, "overflow") != 0) ||
        session.rx_barrier.active || (raw_count != 2U) ||
        (expect_session(rvrt_transport_send_frames(&session, &send_frame, 1U),
                        RVRT_RUNTIME_FAULTED, "send after overflow") != 0) ||
        (rvrt_transport_sync_wait_payload_for_thread(
             &session, 0U, &sync_mode, 0x1000000U, 1U, &raw_frames,
             &raw_count) != RVRT_RUNTIME_FAULTED)) {
        goto cleanup;
    }

    (void)rvrt_transport_deinit(&session);
    mock_runtime_reset();
    memset(&session, 0, sizeof(session));
    if (init_session(&file.artifact, &session, rx_storage, 2U) != 0) {
        goto cleanup;
    }
    const rvrt_frame_t rejected_frames[] = {data_frame(0U, 1U),
                                            complete_frame()};
    mock_runtime_queue_rx(rejected_frames, 2U);
    if ((expect_session(
             rvrt_transport_sync_wait_until_for_thread(
                 &session, 0U, 1U, 10U, &sync_mode, &completed_timesteps, NULL,
                 NULL, reject_frame_handler, NULL),
             RVRT_RUNTIME_RUNTIME_ERROR, "handler rejection") != 0) ||
        (expect_session(rvrt_transport_send_frames(&session, &send_frame, 1U),
                        RVRT_RUNTIME_FAULTED,
                        "send after handler rejection") != 0)) {
        goto cleanup;
    }

    result = 0;
cleanup:
    (void)rvrt_transport_deinit(&session);
    free_artifact(&file);
    return result;
}

int main(void)
{
    if ((verify_manual_flow() != 0) || (verify_model_reset() != 0) ||
        (verify_input_send_api() != 0) || (verify_session_lifecycle() != 0) ||
        (verify_sync_modes() != 0) || (verify_large_irq_drain() != 0) ||
        (verify_barrier_recovery() != 0)) {
        return 1;
    }

    puts("runtime control tests passed");
    return 0;
}
