#include <stdio.h>
#include <string.h>

#include "artifact_reader.h"
#include "data.h"
#include "debug.h"
#include "runtime_session.h"
#include "thread_runner.h"

extern const uint8_t _binary_generated_compile_artifacts_bin_start[];
/* This absolute linker symbol's address is the embedded binary's byte count. */
extern const uint8_t _binary_generated_compile_artifacts_bin_size[];
#define APP_TITLE "mnist"
#define APP_TIMESTEPS MNIST_INPUT_TIMESTEPS
#define APP_INPUT_BYTES MNIST_INPUT_BYTES
#define APP_OUTPUT_ELEMENTS MNIST_OUTPUT_ELEMENTS
#define APP_TIMEOUT_MS 2000U

/* One DATA frame per output element and timestep, plus the complete frame. */
#ifndef RVRT_APP_RX_FRAMES
#define RVRT_APP_RX_FRAMES (APP_TIMESTEPS * APP_OUTPUT_ELEMENTS + 1U)
#endif

#if RVRT_APP_RX_FRAMES < (APP_TIMESTEPS * APP_OUTPUT_ELEMENTS + 1U)
#error "RVRT_APP_RX_FRAMES must hold every expected DATA and complete frame"
#endif

static rvrt_frame_t g_rx_frames[RVRT_APP_RX_FRAMES];
static uint8_t g_input[APP_TIMESTEPS * APP_INPUT_BYTES];

static size_t binary_size(const uint8_t *size_symbol)
{
    return (size_t)(uintptr_t)size_symbol;
}

static int fail_artifact(const char *stage, rvrt_artifact_status_t status)
{
    printf("%s: artifact %s failed: %s\r\n", APP_TITLE, stage,
           rvrt_artifact_status_string(status));
    return 1;
}

static int fail_runtime(const char *stage, rvrt_runtime_status_t status)
{
    printf("%s: session %s failed: %s\r\n", APP_TITLE, stage,
           rvrt_runtime_session_status_string(status));
    return 1;
}

int main(void)
{
    rv_debug_set_level(RV_DEBUG_ERROR);

    int result = 0;
    rvrt_runtime_session_t session = {0};
    rvrt_thread_runner_t runner = {0};

    /* _start is flash data; _size's address converts to its byte count. */
    const uint8_t *const artifact_data =
        _binary_generated_compile_artifacts_bin_start;
    const size_t artifact_size =
        binary_size(_binary_generated_compile_artifacts_bin_size);

    /* 解析 artifact，并取得同一 thread 的运行参数与 I/O mapping */
    rvrt_artifact_view_t artifact = {0};
    rvrt_artifact_status_t status =
        rvrt_artifact_read(artifact_data, artifact_size, &artifact);
    if (status != RVRT_ARTIFACT_OK) {
        result = fail_artifact("read", status);
        goto cleanup;
    }

    rvrt_artifact_runtime_t runtime = {0};
    rvrt_artifact_input_mapping_view_t input_view = {0};
    rvrt_artifact_output_mapping_view_t output_view = {0};
    status = rvrt_artifact_thread_runtime(&artifact, 0U, &runtime);
    if (status != RVRT_ARTIFACT_OK) {
        result = fail_artifact("runtime", status);
        goto cleanup;
    }
    status =
        rvrt_artifact_get_input_mapping_view(&artifact, 0U, 0U, &input_view);
    if (status != RVRT_ARTIFACT_OK) {
        result = fail_artifact("input mapping", status);
        goto cleanup;
    }
    status =
        rvrt_artifact_get_output_mapping_view(&artifact, 0U, 0U, &output_view);
    if (status != RVRT_ARTIFACT_OK) {
        result = fail_artifact("output mapping", status);
        goto cleanup;
    }

    /* 在访问 PAICORE 前确认 artifact 与应用静态资源属于同一模型 */
    if ((runtime.timesteps != APP_TIMESTEPS) ||
        (runtime.output_time_encoding != RVRT_OUTPUT_TIME_ENCODING_STREAM) ||
        (runtime.completion_sync_timestep !=
         runtime.pipeline_latency + runtime.timesteps - 1U) ||
        (input_view.entry_count != APP_INPUT_BYTES) ||
        (input_view.element_count != APP_INPUT_BYTES) ||
        (output_view.kind != RVRT_OUTPUT_DATA) ||
        (output_view.entry_count != APP_OUTPUT_ELEMENTS) ||
        (output_view.element_count != APP_OUTPUT_ELEMENTS)) {
        printf("%s: artifact contract mismatch\r\n", APP_TITLE);
        result = 1;
        goto cleanup;
    }

    const rvrt_runtime_session_open_config_t session_config = {
        .artifact_data = artifact_data,
        .artifact_size = artifact_size,
        .frame_buffer = g_rx_frames,
        .frame_capacity = RVRT_APP_RX_FRAMES,
    };
    rvrt_runtime_status_t session_status =
        rvrt_runtime_session_open(&session, &session_config);
    if (session_status != RVRT_RUNTIME_OK) {
        result = fail_runtime("open", session_status);
        goto cleanup;
    }
    session_status = rvrt_runtime_session_configure(&session);
    if (session_status != RVRT_RUNTIME_OK) {
        result = fail_runtime("configure", session_status);
        goto cleanup;
    }
    const rvrt_thread_runner_open_config_t runner_config = {
        .session = &session,
        .thread_index = 0U,
        .input_mapping_index = 0U,
        .output_mapping_index = 0U,
        .timeout_ms = APP_TIMEOUT_MS,
    };
    session_status = rvrt_thread_runner_open(&runner, &runner_config);
    if (session_status != RVRT_RUNTIME_OK) {
        result = fail_runtime("runner open", session_status);
        goto cleanup;
    }

    for (uint32_t sample = 0U; sample < MNIST_SAMPLE_COUNT; ++sample) {
        /* 每个独立样本从确定状态开始；流式应用不会在样本间调用此接口。 */
        mnist_build_input(sample, g_input);
        uint8_t output[APP_TIMESTEPS * APP_OUTPUT_ELEMENTS] = {0};
        session_status = rvrt_thread_runner_run_sample(
            &runner, g_input, sizeof(g_input), APP_INPUT_BYTES, output,
            sizeof(output), APP_OUTPUT_ELEMENTS);
        if (session_status != RVRT_RUNTIME_OK) {
            result = fail_runtime("run sample", session_status);
            goto cleanup;
        }
        if (memcmp(output, mnist_expected_output[sample], sizeof(output)) !=
            0) {
            printf("%s: sample=%u output mismatch\r\n", APP_TITLE,
                   (unsigned)sample);
            result = 1;
            goto cleanup;
        }

        /* spike sum 和 argmax 是 MNIST 业务后处理。 */
        uint32_t sums[APP_OUTPUT_ELEMENTS] = {0};
        for (uint32_t timestep = 0U; timestep < APP_TIMESTEPS; ++timestep) {
            for (uint32_t elem = 0U; elem < APP_OUTPUT_ELEMENTS; ++elem) {
                sums[elem] += output[timestep * APP_OUTPUT_ELEMENTS + elem];
            }
        }
        uint32_t prediction = 0U;
        for (uint32_t elem = 1U; elem < APP_OUTPUT_ELEMENTS; ++elem) {
            if (sums[elem] > sums[prediction]) {
                prediction = elem;
            }
        }
        if (prediction != mnist_expected_labels[sample]) {
            printf("%s: sample=%u prediction=%u expected=%u\r\n", APP_TITLE,
                   (unsigned)sample, (unsigned)prediction,
                   (unsigned)mnist_expected_labels[sample]);
            result = 1;
            goto cleanup;
        }

        printf("%s: sample=%u prediction=%u PASS\r\n", APP_TITLE,
               (unsigned)sample, (unsigned)prediction);
    }

    printf("%s: samples=%u decoded=%ux%u MNIST_PASS\r\n", APP_TITLE,
           (unsigned)MNIST_SAMPLE_COUNT, (unsigned)runtime.timesteps,
           (unsigned)output_view.element_count);

cleanup:
    if (runner.opened) {
        (void)rvrt_thread_runner_close(&runner);
    }
    if (session.opened) {
        (void)rvrt_runtime_session_close(&session);
    }
    return result;
}
