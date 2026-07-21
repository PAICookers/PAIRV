/*
 * run_layers_host.c —— host（x86）编解码 buffer 压测 driver（方案C）。
 *
 * 背景：fixtures/ 里的 artifact 是过期的 timesteps=8，而层的单步契约要求 timesteps=1，
 * 直接调层函数会卡在 validate。本 driver 绕开 validate，用层的真实共享 buffer
 * （layer_frame_buf / tensor_workspace）+ 真实的 input_view/output_view（逐步映射
 * 768→1536 本来就对）+ 手构的单步 runtime（timesteps=1），把 SNN Head 各层实际会
 * 发起的"编码输入帧 / 解码输出帧"调用完整跑一遍：
 *
 *   send_input_timestep  -> 真实把 int8 输入按 input_view 编码进 layer_frame_buf
 *   sync_wait            -> mock 把 work×N + complete 帧灌进 RX(layer_frame_buf)
 *   decode_output_frames -> DATA 层：批量解码写回 tensor_workspace 当前行(1536B)
 *   decode_voltage_frame -> VOLTAGE 层(fc2)：逐帧拼 int32 写回当前行(6144B)
 *
 * 整个可执行文件用 -fsanitize=address,undefined 编译，只要编解码对这些共享 buffer
 * 出现越界/覆盖/未对齐，ASan/UBSan 会在运行时立刻中止并打印精确位置。不校验数值。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "snn_head_golden.h"
#include "snn_head_internal.h"

int snn_head_host_load_artifacts(const char *fixture_dir);
void host_mock_set_rx_frames(const rvrt_frame_t *frames, uint32_t count);

#ifndef SNN_HEAD_FIXTURE_DIR
#define SNN_HEAD_FIXTURE_DIR "fixtures"
#endif

/* 帧 high 位型（见 frame_codec.h）：WORK 类型在 bit[31:30]=2；DATA/VOLTAGE 子型在
 * bit29；COMPLETE 在 bit[31:28]=0xE。 */
#define HOST_WORK_DATA_HIGH 0x80000000U    /* type=WORK, kind=DATA */
#define HOST_WORK_VOLTAGE_HIGH 0xA0000000U /* type=WORK, kind=VOLTAGE */
#define HOST_COMPLETE_HIGH 0xE0000000U

/* driver 拥有的注入帧缓冲（与 layer_frame_buf 分离，避免别名）。 */
static rvrt_frame_t g_inject[SNN_HEAD_FRAME_BUF_FRAMES];

/* 把 runtime 内部的 RV_DEBUG_LOGE/W/I 直接转到 stderr，便于定位失败步骤。 */
static void host_debug_sink(rv_debug_level_t level, const char *title,
                            const char *function_name, const char *message,
                            void *user_data)
{
    (void)user_data;
    fprintf(stderr, "[rvdbg L%d] %s/%s: %s\n", (int)level,
            title ? title : "?", function_name ? function_name : "?",
            message ? message : "");
}

static int report(const char *name, bool ok)
{
    printf("%-8s : %s\n", name, ok ? "OK" : "RUN FAILED");
    return ok ? 0 : 1;
}

/* 预置本轮 sync_wait 要投递的 RX 帧：work_frames 个 work 帧（地址取 0..N-1，部分会
 * 命中 output_view 真正写入，其余安全跳过）+ 末尾 1 个 complete。填到接近 RX 容量，
 * 压测 ISR 追加与解码遍历。 */
static void arm_injection(bool is_voltage, uint32_t work_frames)
{
    const uint32_t high = is_voltage ? HOST_WORK_VOLTAGE_HIGH : HOST_WORK_DATA_HIGH;
    uint32_t n = 0U;
    for (uint32_t i = 0U; (i < work_frames) && (n < SNN_HEAD_FRAME_BUF_FRAMES - 1U);
         ++i) {
        g_inject[n].high = high;
        g_inject[n].low = (i << 8U) | 0xA5U; /* axon_bit_idx=i, timestep=0, payload */
        ++n;
    }
    g_inject[n].high = HOST_COMPLETE_HIGH;
    g_inject[n].low = 0U;
    ++n;
    host_mock_set_rx_frames(g_inject, n);
}

/*
 * 对一层做端到端编解码压测：建立 session -> load_config -> reset -> 8 轮
 * (send -> sync -> decode)。用手构的单步 runtime 纠正过期 fixture 的 timesteps。
 */
static int stress_codec(const char *name, const uint8_t *art_start,
                        const uint8_t *art_size, uint32_t input_elems,
                        bool is_voltage)
{
    snn_head_layer_artifact_context_t ctx = {0};
    if (!snn_head_read_layer_artifact(art_start, art_size, &ctx)) {
        return report(name, false);
    }

    rvrt_session_t session = {0};
    const rvrt_session_config_t cfg = {
        .artifact = &ctx.artifact,
        .thread_index = 0U,
        .rx_frames = layer_frame_buf,
        .rx_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
    };
    if (rvrt_session_init(&session, &cfg) != RVRT_SESSION_OK) {
        return report(name, false);
    }
    if (rvrt_session_load_config(&session) != RVRT_SESSION_OK) {
        return report(name, false);
    }
    if (rvrt_session_reset_model(&session, SNN_HEAD_TIMEOUT_MS) !=
        RVRT_SESSION_OK) {
        return report(name, false);
    }

    /* 单步 runtime：纠正过期 fixture 的 timesteps=8，让批量解码 required=element_count
     * 与层的单行输出 buffer 尺寸一致。 */
    const rvrt_artifact_runtime_t rt1 = {
        .timesteps = 1U,
        .tick_depth = 1U,
        .sync_steps = 1U,
        .decode_mode = RVRT_DECODE_MODE_STREAM,
    };

    /* 每行输出字节数：DATA=1536（uint8 spike），VOLTAGE=6144（int32 膜电位）。 */
    const uint32_t row_bytes =
        is_voltage ? SNN_HEAD_VOLTAGE_STRIDE_BYTES : SNN_HEAD_HIDDEN_DIM;
    /* 注入 work 帧数：DATA 一行最坏 1536；VOLTAGE 每元素 4 lane = 6144。 */
    const uint32_t work_frames =
        is_voltage ? (SNN_HEAD_HIDDEN_DIM * 4U) : SNN_HEAD_HIDDEN_DIM;

    for (uint32_t t = 0U; t < SNN_HEAD_TIMESTEPS; ++t) {
        uint8_t *const row = &tensor_workspace[t * row_bytes];

        /* 编码：真实把当前行 int8 输入按 input_view 编码进 layer_frame_buf。 */
        if (rvrt_session_send_input_timestep(&session, &ctx.input_view, 0U, row,
                                             input_elems, layer_frame_buf,
                                             RVRT_MAX_WORKSPACE_FRAMES) !=
            RVRT_SESSION_OK) {
            return report(name, false);
        }

        /* 预置本轮 RX 帧，随后 sync_wait 的 armed 屏障会把它们灌进 RX buffer。 */
        arm_injection(is_voltage, work_frames);

        const rvrt_frame_t *frames = NULL;
        uint32_t count = 0U;
        if (rvrt_session_sync_wait(&session, ctx.runtime.tick_depth,
                                   SNN_HEAD_TIMEOUT_MS, &frames, &count) !=
            RVRT_SESSION_OK) {
            return report(name, false);
        }

        if (is_voltage) {
            /* VOLTAGE：逐帧拼 int32 写回本行（先清零本行与 lane 拼接状态）。 */
            int32_t *const vrow = (int32_t *)row;
            memset(vrow, 0, SNN_HEAD_VOLTAGE_STRIDE_BYTES);
            memset(fc2_voltage_state, 0, sizeof(fc2_voltage_state));
            for (uint32_t i = 0U; i < count; ++i) {
                bool written = false;
                if (rvrt_decode_voltage_frame(&ctx.output_view, &frames[i], vrow,
                                              SNN_HEAD_HIDDEN_DIM,
                                              fc2_voltage_state,
                                              SNN_HEAD_HIDDEN_DIM,
                                              &written) != RVRT_STATUS_OK) {
                    return report(name, false);
                }
            }
        } else {
            /* DATA：批量解码写回本行（decode 内部先按 required 清零再写命中元素）。 */
            if (rvrt_decode_output_frames(&ctx.output_view, &rt1, frames, count,
                                          row, SNN_HEAD_HIDDEN_DIM) !=
                RVRT_STATUS_OK) {
                return report(name, false);
            }
        }
    }
    return report(name, true);
}

int main(void)
{
    rv_debug_set_sink(host_debug_sink, NULL);
    rv_debug_set_level(RV_DEBUG_DEBUG);
    if (snn_head_host_load_artifacts(SNN_HEAD_FIXTURE_DIR) != 0) {
        fprintf(stderr, "failed to load PAICore artifacts from %s\n",
                SNN_HEAD_FIXTURE_DIR);
        return 2;
    }
    printf("== snn_head host codec buffer stress (ASan/UBSan) ==\n");
    printf("golden_ready=%d\n", snn_head_golden_ready);

    int fail = 0;
    /* fc1_lif：输入 int8[768]，输出 DATA/spike[1536]。 */
    fail |= stress_codec("fc1_lif", snn_head_fc1_lif_artifact_start,
                         snn_head_fc1_lif_artifact_size, SNN_HEAD_INPUT_DIM,
                         false);
    /* block0/block1：输入 int8[1536]，输出 DATA/spike[1536]。 */
    fail |= stress_codec("block0", snn_head_block0_lif_artifact_start,
                         snn_head_block0_lif_artifact_size, SNN_HEAD_HIDDEN_DIM,
                         false);
    fail |= stress_codec("block1", snn_head_block1_lif_artifact_start,
                         snn_head_block1_lif_artifact_size, SNN_HEAD_HIDDEN_DIM,
                         false);
    /* fc2：输入 int8[1536]，输出 VOLTAGE/int32[1536]。 */
    fail |= stress_codec("fc2", snn_head_fc2_artifact_start,
                         snn_head_fc2_artifact_size, SNN_HEAD_HIDDEN_DIM, true);

    printf("\nHOST CODEC STRESS: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
