#include "artifact_executor.h"
#include "artifact_reader.h"
#include "frame_codec.h"
#include "mock_runtime_hw.h"
#include "session_io.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RVRT_TEST_ASSET_DIR
#define RVRT_TEST_ASSET_DIR "assets"
#endif

#define TEST_ALIGNMENT 8U
#define TEST_PATH_BYTES 512U
#define TEST_DATA_HIGH 0x80000000U
#define TEST_VOLTAGE_HIGH 0xA0000000U
#define TEST_COMPLETE_HIGH 0xE0000000U

typedef struct test_artifact_s {
    uint8_t *data;
    rvrt_artifact_t artifact;
} test_artifact_t;

static rvrt_frame_t data_frame(uint32_t axon_bit_idx, uint8_t payload)
{
    return (rvrt_frame_t){TEST_DATA_HIGH,
                          (axon_bit_idx << 8U) | (uint32_t)payload};
}

static rvrt_frame_t voltage_frame(uint32_t base, uint32_t lane, uint32_t value)
{
    const uint8_t payload = (uint8_t)(value >> (lane * 8U));
    return (rvrt_frame_t){TEST_VOLTAGE_HIGH,
                          ((base + lane * 8U) << 8U) | payload};
}

static rvrt_frame_t complete_frame(void)
{
    return (rvrt_frame_t){TEST_COMPLETE_HIGH, 0U};
}

static int read_artifact(const char *name, test_artifact_t *out)
{
    char path[TEST_PATH_BYTES];
    const int length =
        snprintf(path, sizeof(path), "%s/%s", RVRT_TEST_ASSET_DIR, name);
    if ((length < 0) || ((size_t)length >= sizeof(path))) {
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
    if (rvrt_artifact_read(data, (size_t)file_size, &out->artifact) !=
        RVRT_ARTIFACT_OK) {
        free(data);
        out->data = NULL;
        return 1;
    }
    return 0;
}

static void free_artifact(test_artifact_t *artifact)
{
    free(artifact->data);
    artifact->data = NULL;
}

static int expect_session(rvrt_session_status_t actual,
                          rvrt_session_status_t expected, const char *stage)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s status=%s expected=%s\n", stage,
            rvrt_session_status_string(actual),
            rvrt_session_status_string(expected));
    return 1;
}

static int init_session(const rvrt_artifact_t *artifact,
                        rvrt_session_t *session, rvrt_frame_t *rx_frames,
                        uint32_t rx_capacity)
{
    const rvrt_session_config_t config = {
        artifact,
        0U,
        rx_frames,
        rx_capacity,
    };
    return expect_session(rvrt_session_init(session, &config), RVRT_SESSION_OK,
                          "session init");
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
    rvrt_session_t session = {0};
    if ((init_session(&file.artifact, &session, rx_storage, 8U) != 0) ||
        (expect_session(rvrt_session_load_config(&session), RVRT_SESSION_OK,
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
    if ((rvrt_session_send_input_timestep(&session, &input_view, 0U, input,
                                          sizeof(input), input_workspace,
                                          1U) != RVRT_SESSION_OK) ||
        (rvrt_session_send_input_timestep(&session, &input_view, 1U, input,
                                          sizeof(input), input_workspace,
                                          1U) != RVRT_SESSION_OK) ||
        (mock_runtime_sent_count() != 5U)) {
        goto cleanup;
    }

    const rvrt_frame_t received[] = {
        data_frame(0U, 0x11U),
        data_frame(1U, 0x22U),
        complete_frame(),
    };
    mock_runtime_queue_rx(received, 3U);
    mock_runtime_probe_armed(&session);
    const rvrt_frame_t *raw_frames = NULL;
    uint32_t raw_count = 0U;
    if ((expect_session(
             rvrt_session_sync_wait(&session, 2U, 10U, &raw_frames, &raw_count),
             RVRT_SESSION_OK, "manual barrier") != 0) ||
        (raw_frames != rx_storage) || (raw_count != 3U) ||
        !rvrt_frame_is_complete(&raw_frames[2]) ||
        (mock_runtime_sent_count() != 6U) ||
        ((mock_runtime_sent_frames()[5].low & 0xFFFFFFU) != 2U) ||
        (mock_runtime_nested_send_status() != RVRT_SESSION_RUNTIME_ERROR) ||
        (mock_runtime_nested_sync_status() != RVRT_SESSION_RUNTIME_ERROR)) {
        goto cleanup;
    }

    uint8_t output[2] = {0U, 0U};
    for (uint32_t i = 0U; i < raw_count; ++i) {
        bool written = false;
        if (rvrt_decode_output_frame(&output_view, &raw_frames[i], output,
                                     sizeof(output),
                                     &written) != RVRT_STATUS_OK) {
            goto cleanup;
        }
    }
    if ((output[0] != 0x11U) || (output[1] != 0x22U)) {
        goto cleanup;
    }

    rvrt_executor_buffer_t dummy_buffer = {output, sizeof(output)};
    rvrt_frame_t workspace[1];
    rvrt_artifact_executor_config_t executor_config = {
        &session, &dummy_buffer, 1U, workspace, 1U, NULL, 0U,
    };
    rvrt_artifact_executor_t executor = {0};
    if (rvrt_artifact_executor_init(&executor, &executor_config) !=
        RVRT_SESSION_RUNTIME_ERROR) {
        goto cleanup;
    }

    result = 0;
cleanup:
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
    rvrt_session_t session = {0};
    rvrt_artifact_input_mapping_view_t input_view = {0};
    const uint8_t input[] = {3U, 4U};
    const uint8_t zero_input[] = {0U, 0U};
    rvrt_frame_t one_frame_workspace[1];
    rvrt_frame_t max_workspace[RVRT_MAX_WORKSPACE_FRAMES];
    rvrt_frame_t expected[2];

    mock_runtime_reset();
    if ((init_session(&file.artifact, &session, rx_storage, 1U) != 0) ||
        (rvrt_artifact_get_input_mapping_view(
             &file.artifact, 0U, 0U, &input_view) != RVRT_ARTIFACT_OK) ||
        (rvrt_session_send_input_timestep(&session, &input_view, 7U, input,
                                          sizeof(input), one_frame_workspace,
                                          1U) != RVRT_SESSION_OK) ||
        (mock_runtime_sent_count() != 2U)) {
        goto cleanup;
    }
    memcpy(expected, mock_runtime_sent_frames(), sizeof(expected));

    mock_runtime_reset();
    memset(&session, 0, sizeof(session));
    if ((init_session(&file.artifact, &session, rx_storage, 1U) != 0) ||
        (rvrt_session_send_input_timestep(
             &session, &input_view, 7U, input, sizeof(input), max_workspace,
             RVRT_MAX_WORKSPACE_FRAMES) != RVRT_SESSION_OK) ||
        (mock_runtime_sent_count() != 2U) ||
        (memcmp(mock_runtime_sent_frames(), expected, sizeof(expected)) != 0) ||
        (rvrt_session_send_input_timestep(
             &session, &input_view, 8U, zero_input, sizeof(zero_input),
             max_workspace, RVRT_MAX_WORKSPACE_FRAMES) != RVRT_SESSION_OK) ||
        (mock_runtime_sent_count() != 2U)) {
        goto cleanup;
    }

    if ((rvrt_session_send_input_timestep(&session, &input_view, 0U, input,
                                          sizeof(input), max_workspace,
                                          0U) != RVRT_SESSION_RUNTIME_ERROR) ||
        (rvrt_session_send_input_timestep(
             &session, &input_view, 0U, input, sizeof(input), max_workspace,
             RVRT_MAX_WORKSPACE_FRAMES + 1U) != RVRT_SESSION_RUNTIME_ERROR) ||
        (rvrt_session_send_input_timestep(
             &session, &input_view, 0U, input, 0U, max_workspace,
             RVRT_MAX_WORKSPACE_FRAMES) != RVRT_SESSION_RUNTIME_ERROR) ||
        (rvrt_session_send_input_timestep(
             &session, NULL, 0U, input, sizeof(input), max_workspace,
             RVRT_MAX_WORKSPACE_FRAMES) != RVRT_SESSION_RUNTIME_ERROR) ||
        (rvrt_session_send_input_timestep(
             &session, &input_view, 0U, NULL, sizeof(input), max_workspace,
             RVRT_MAX_WORKSPACE_FRAMES) != RVRT_SESSION_RUNTIME_ERROR) ||
        (rvrt_session_send_input_timestep(
             &session, &input_view, 0U, input, sizeof(input), NULL,
             RVRT_MAX_WORKSPACE_FRAMES) != RVRT_SESSION_RUNTIME_ERROR) ||
        (rvrt_session_send_input_timestep(
             NULL, &input_view, 0U, input, sizeof(input), max_workspace,
             RVRT_MAX_WORKSPACE_FRAMES) != RVRT_SESSION_RUNTIME_ERROR)) {
        goto cleanup;
    }

    session.phase.armed = true;
    if (rvrt_session_send_input_timestep(
            &session, &input_view, 0U, input, sizeof(input), max_workspace,
            RVRT_MAX_WORKSPACE_FRAMES) != RVRT_SESSION_RUNTIME_ERROR ||
        (mock_runtime_sent_count() != 2U)) {
        goto cleanup;
    }
    session.phase.armed = false;

    result = 0;
cleanup:
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
    rvrt_session_t session = {0};
    const rvrt_frame_t send_frame = {0U, 0U};
    const rvrt_frame_t *raw_frames = NULL;
    uint32_t raw_count = 0U;

    mock_runtime_reset();
    if (init_session(&file.artifact, &session, rx_storage, 2U) != 0) {
        goto cleanup;
    }
    mock_runtime_set_auto_irq(false);
    if ((expect_session(
             rvrt_session_sync_wait(&session, 1U, 1U, &raw_frames, &raw_count),
             RVRT_SESSION_TIMEOUT, "timeout") != 0) ||
        session.phase.armed ||
        (expect_session(rvrt_session_send_frames(&session, &send_frame, 1U),
                        RVRT_SESSION_OK, "send after timeout") != 0)) {
        goto cleanup;
    }

    mock_runtime_reset();
    memset(&session, 0, sizeof(session));
    if (init_session(&file.artifact, &session, rx_storage, 2U) != 0) {
        goto cleanup;
    }
    const rvrt_frame_t overflow_frames[] = {
        data_frame(0U, 1U), data_frame(1U, 2U), complete_frame()};
    mock_runtime_queue_rx(overflow_frames, 3U);
    if ((expect_session(
             rvrt_session_sync_wait(&session, 1U, 10U, &raw_frames, &raw_count),
             RVRT_SESSION_OVERFLOW, "overflow") != 0) ||
        session.phase.armed || (raw_count != 2U) ||
        (expect_session(rvrt_session_send_frames(&session, &send_frame, 1U),
                        RVRT_SESSION_OK, "send after overflow") != 0) ||
        (rvrt_session_sync_wait(&session, 0x1000000U, 1U, &raw_frames,
                                &raw_count) != RVRT_SESSION_RUNTIME_ERROR)) {
        goto cleanup;
    }

    result = 0;
cleanup:
    free_artifact(&file);
    return result;
}

static int verify_data_executor(void)
{
    test_artifact_t file = {0};
    if (read_artifact("compile_artifacts.bin", &file) != 0) {
        return 1;
    }

    int result = 1;
    mock_runtime_reset();
    rvrt_frame_t rx_storage[10];
    rvrt_session_t session = {0};
    if (init_session(&file.artifact, &session, rx_storage, 10U) != 0) {
        goto cleanup;
    }

    uint8_t input_buffer[1];
    uint8_t output_buffer[9];
    rvrt_executor_buffer_t buffers[] = {
        {input_buffer, sizeof(input_buffer)},
        {output_buffer, sizeof(output_buffer)},
    };
    rvrt_frame_t workspace[1];
    rvrt_artifact_executor_config_t config = {
        &session, buffers, 2U, workspace, 1U, NULL, 0U,
    };
    rvrt_artifact_executor_t executor = {0};
    if (rvrt_artifact_executor_init(&executor, &config) != RVRT_SESSION_OK) {
        goto cleanup;
    }
    config.workspace_capacity = RVRT_MAX_WORKSPACE_FRAMES + 1U;
    if (rvrt_artifact_executor_init(&executor, &config) !=
        RVRT_SESSION_RUNTIME_ERROR) {
        goto cleanup;
    }
    config.workspace_capacity = 1U;

    static const uint8_t expected[9] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U};
    rvrt_frame_t received[10];
    for (uint32_t i = 0U; i < 9U; ++i) {
        received[i] = data_frame(i, expected[i]);
    }
    received[9] = complete_frame();
    mock_runtime_queue_rx(received, 10U);

    const uint8_t input = 1U;
    const uint8_t *output = NULL;
    size_t output_size = 0U;
    if ((rvrt_artifact_executor_run(&executor, &input, 1U, 10U) !=
         RVRT_SESSION_OK) ||
        (rvrt_artifact_executor_get_output(&executor, &output, &output_size) !=
         RVRT_SESSION_OK) ||
        (output_size != sizeof(expected)) ||
        (memcmp(output, expected, sizeof(expected)) != 0)) {
        goto cleanup;
    }

    result = 0;
cleanup:
    free_artifact(&file);
    return result;
}

static int verify_voltage_executor(void)
{
    static const uint32_t bases[9] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 32U};
    static const uint32_t expected[9] = {
        0x12345678U, 0xFEDCBA98U, 3U, 4U, 5U, 6U, 7U, 8U, 0x80000001U,
    };
    test_artifact_t file = {0};
    if (read_artifact("compile_artifacts_voltage.bin", &file) != 0) {
        return 1;
    }

    int result = 1;
    mock_runtime_reset();
    rvrt_frame_t rx_storage[37];
    rvrt_session_t session = {0};
    if (init_session(&file.artifact, &session, rx_storage, 37U) != 0) {
        goto cleanup;
    }

    uint8_t input_buffer[1];
    int32_t output_buffer[9];
    rvrt_executor_buffer_t buffers[] = {
        {input_buffer, sizeof(input_buffer)},
        {(uint8_t *)(void *)output_buffer, sizeof(output_buffer)},
    };
    rvrt_frame_t workspace[1];
    rvrt_voltage_decode_state_t voltage_state[9];
    const rvrt_artifact_executor_config_t config = {
        &session, buffers, 2U, workspace, 1U, voltage_state, 9U,
    };
    rvrt_artifact_executor_t executor = {0};
    if (rvrt_artifact_executor_init(&executor, &config) != RVRT_SESSION_OK) {
        goto cleanup;
    }

    rvrt_frame_t received[37];
    uint32_t frame_index = 0U;
    for (uint32_t elem = 0U; elem < 9U; ++elem) {
        for (uint32_t lane = 0U; lane < 4U; ++lane) {
            received[frame_index++] =
                voltage_frame(bases[elem], lane, expected[elem]);
        }
    }
    received[frame_index] = complete_frame();
    mock_runtime_queue_rx(received, 37U);

    const uint8_t input = 1U;
    if ((rvrt_artifact_executor_run(&executor, &input, 1U, 10U) !=
         RVRT_SESSION_OK) ||
        (memcmp(output_buffer, expected, sizeof(expected)) != 0)) {
        goto cleanup;
    }

    result = 0;
cleanup:
    free_artifact(&file);
    return result;
}

static int verify_cpu_executor(void)
{
    test_artifact_t file = {0};
    if (read_artifact("compile_artifacts_cpu.bin", &file) != 0) {
        return 1;
    }

    int result = 1;
    mock_runtime_reset();
    rvrt_frame_t rx_storage[3];
    rvrt_session_t session = {0};
    if (init_session(&file.artifact, &session, rx_storage, 3U) != 0) {
        goto cleanup;
    }

    uint8_t buffer0[2];
    uint8_t buffer1[2];
    uint8_t buffer2[2];
    rvrt_executor_buffer_t buffers[] = {
        {buffer0, sizeof(buffer0)},
        {buffer1, sizeof(buffer1)},
        {buffer2, sizeof(buffer2)},
    };
    rvrt_frame_t workspace[1];
    const rvrt_artifact_executor_config_t config = {
        &session, buffers, 3U, workspace, 1U, NULL, 0U,
    };
    rvrt_artifact_executor_t executor = {0};
    if (rvrt_artifact_executor_init(&executor, &config) != RVRT_SESSION_OK) {
        goto cleanup;
    }

    const uint8_t input[] = {5U, 6U};
    const rvrt_frame_t received[] = {
        data_frame(0U, input[0]), data_frame(1U, input[1]), complete_frame()};
    mock_runtime_queue_rx(received, 3U);
    const uint8_t *output = NULL;
    size_t output_size = 0U;
    if ((rvrt_artifact_executor_run(&executor, input, sizeof(input), 10U) !=
         RVRT_SESSION_OK) ||
        (rvrt_artifact_executor_get_output(&executor, &output, &output_size) !=
         RVRT_SESSION_OK) ||
        (output_size != sizeof(input)) ||
        (memcmp(output, input, sizeof(input)) != 0) ||
        (mock_runtime_task_run_count() != 1U) ||
        (mock_runtime_sent_count() != 3U)) {
        goto cleanup;
    }

    result = 0;
cleanup:
    free_artifact(&file);
    return result;
}

int main(void)
{
    if ((verify_manual_flow() != 0) || (verify_input_send_api() != 0) ||
        (verify_barrier_recovery() != 0) || (verify_data_executor() != 0) ||
        (verify_voltage_executor() != 0) || (verify_cpu_executor() != 0)) {
        return 1;
    }

    puts("runtime control tests passed");
    return 0;
}
