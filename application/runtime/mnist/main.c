#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "artifact_reader.h"
#include "data.h"
#include "debug.h"
#include "frame_codec.h"
#include "session.h"
#include "session_io.h"

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
static rvrt_frame_t g_workspace[32U];
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

static int fail_runtime(const char *stage, rvrt_status_t status)
{
    printf("%s: %s failed: %s\r\n", APP_TITLE, stage,
           rvrt_status_string(status));
    return 1;
}

static int fail_session(const char *stage, rvrt_session_status_t status)
{
    printf("%s: session %s failed: %s\r\n", APP_TITLE, stage,
           rvrt_session_status_string(status));
    return 1;
}

int main(void)
{
    rv_debug_set_level(RV_DEBUG_ERROR);

    /* _start is flash data; _size's address converts to its byte count. */
    const uint8_t *const artifact_data =
        _binary_generated_compile_artifacts_bin_start;
    const size_t artifact_size =
        binary_size(_binary_generated_compile_artifacts_bin_size);
    const uint32_t workspace_capacity =
        (RVRT_MAX_WORKSPACE_FRAMES <
         (uint32_t)(sizeof(g_workspace) / sizeof(g_workspace[0])))
            ? RVRT_MAX_WORKSPACE_FRAMES
            : (uint32_t)(sizeof(g_workspace) / sizeof(g_workspace[0]));

    /* 解析 artifact，并取得同一 thread 的运行参数与 I/O mapping */
    rvrt_artifact_t artifact = {0};
    rvrt_artifact_status_t status =
        rvrt_artifact_read(artifact_data, artifact_size, &artifact);
    if (status != RVRT_ARTIFACT_OK) {
        return fail_artifact("read", status);
    }

    rvrt_artifact_runtime_t runtime = {0};
    rvrt_artifact_input_mapping_view_t input_view = {0};
    rvrt_artifact_output_mapping_view_t output_view = {0};
    status = rvrt_artifact_thread_runtime(&artifact, 0U, &runtime);
    if (status != RVRT_ARTIFACT_OK) {
        return fail_artifact("runtime", status);
    }
    status =
        rvrt_artifact_get_input_mapping_view(&artifact, 0U, 0U, &input_view);
    if (status != RVRT_ARTIFACT_OK) {
        return fail_artifact("input mapping", status);
    }
    status =
        rvrt_artifact_get_output_mapping_view(&artifact, 0U, 0U, &output_view);
    if (status != RVRT_ARTIFACT_OK) {
        return fail_artifact("output mapping", status);
    }

    /* 在访问 PAICORE 前确认 artifact 与应用静态资源属于同一模型 */
    if ((runtime.timesteps != APP_TIMESTEPS) ||
        (runtime.decode_mode != RVRT_DECODE_MODE_STREAM) ||
        (runtime.sync_steps != runtime.tick_depth + runtime.timesteps - 1U) ||
        (input_view.entry_count != APP_INPUT_BYTES) ||
        (output_view.kind != RVRT_OUTPUT_DATA) ||
        (output_view.entry_count != APP_OUTPUT_ELEMENTS) ||
        (output_view.element_count != APP_OUTPUT_ELEMENTS)) {
        printf("%s: artifact contract mismatch\r\n", APP_TITLE);
        return 1;
    }

    /* Session 只借用应用提供的 RX storage；配置帧通常每次部署加载一次 */
    rvrt_session_t session = {0};
    const rvrt_session_config_t config = {
        .artifact = &artifact,
        .thread_index = 0U,
        .rx_frames = g_rx_frames,
        .rx_capacity = RVRT_APP_RX_FRAMES,
    };
    rvrt_session_status_t session_status = rvrt_session_init(&session, &config);
    if (session_status != RVRT_SESSION_OK) {
        return fail_session("init", session_status);
    }
    session_status = rvrt_session_load_config(&session);
    if (session_status != RVRT_SESSION_OK) {
        return fail_session("load config", session_status);
    }

    for (uint32_t sample = 0U; sample < MNIST_SAMPLE_COUNT; ++sample) {
        /* 每个独立样本从确定状态开始；流式应用不会在样本间调用此接口。 */
        session_status = rvrt_session_reset_model(&session, APP_TIMEOUT_MS);
        if (session_status != RVRT_SESSION_OK) {
            return fail_session("reset model", session_status);
        }

        mnist_build_input(sample, g_input);

        /* 输入 helper 内部完成 chunk 编码和发送循环。 */
        for (uint32_t timestep = 0U; timestep < APP_TIMESTEPS; ++timestep) {
            session_status = rvrt_session_send_input_timestep(
                &session, &input_view, timestep,
                &g_input[timestep * APP_INPUT_BYTES], APP_INPUT_BYTES,
                g_workspace, workspace_capacity);
            if (session_status != RVRT_SESSION_OK) {
                return fail_session("send input", session_status);
            }
        }

        /* 使用 artifact 的 sync_steps 推进完整流水线。 */
        const rvrt_frame_t *rx_frames = NULL;
        uint32_t rx_frame_count = 0U;
        session_status = rvrt_session_sync_wait(
            &session, runtime.sync_steps, APP_TIMEOUT_MS, &rx_frames,
            &rx_frame_count);
        if (session_status != RVRT_SESSION_OK) {
            return fail_session("sync", session_status);
        }

        /* Runtime 将有效 STREAM DATA 归一化为 [应用时间步][输出元素]。 */
        uint8_t output[APP_TIMESTEPS * APP_OUTPUT_ELEMENTS] = {0};
        rvrt_status_t runtime_status = rvrt_decode_output_frames(
            &output_view, &runtime, rx_frames, rx_frame_count, output,
            sizeof(output));
        if (runtime_status != RVRT_STATUS_OK) {
            return fail_runtime("decode", runtime_status);
        }
        if (memcmp(output, mnist_expected_output[sample], sizeof(output)) != 0) {
            printf("%s: sample=%u output mismatch\r\n", APP_TITLE,
                   (unsigned)sample);
            return 1;
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
            return 1;
        }

        printf("%s: sample=%u prediction=%u PASS\r\n", APP_TITLE,
               (unsigned)sample, (unsigned)prediction);
    }

    printf("%s: samples=%u decoded=%ux%u MNIST_PASS\r\n", APP_TITLE,
           (unsigned)MNIST_SAMPLE_COUNT, (unsigned)runtime.timesteps,
           (unsigned)output_view.element_count);
    return 0;
}
