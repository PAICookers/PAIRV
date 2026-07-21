#include "snn_head.h"
#include "snn_head_internal.h"

/*
 * SNN Head 分层实现的核心文件：三块跨层共享 buffer 的唯一定义、两个公共 helper
 * （读取/校验单层 artifact）、以及公共 API snn_head_run_chunk()
 * （fc1_lif -> block0 -> block1 -> fc2 -> fc3）。各层计算见 snn_head_fc*.c，
 * 宏/参数/artifact 符号/类型/函数声明见 snn_head_internal.h。
 */

/* 主张量 workspace，五层共用，最大 float32/int32[8][1536] = 48KB。底层 uint8_t 字节
 * 缓冲，各层按阶段解释成 float、int8 或 int32，按 stride=1536 行布局原地覆盖推进。
 * aligned(4) 保证转换成 float* 或 int32_t* 时满足对齐。唯一定义在此（外部链接）。 */
uint8_t tensor_workspace[SNN_HEAD_TENSOR_WORKSPACE_BYTES]
    __attribute__((aligned(4)));

/* 层间共享帧 buffer：先当输入编码分块 workspace，发完后当 RX 接收帧 buffer，
 * 单 timestep 闭环下两用途时间不重叠。容量按 fc2 VOLTAGE 6145 帧最坏情况预留。 */
rvrt_frame_t layer_frame_buf[SNN_HEAD_FRAME_BUF_FRAMES];

/* fc2 VOLTAGE 解码的逐元素 lane 拼接状态，每 timestep 解码前整体清零；
 * DATA 层不用，仅 fc2/fc3 复用。 */
rvrt_voltage_decode_state_t fc2_voltage_state[SNN_HEAD_HIDDEN_DIM];

/**
 * @brief 将 objcopy 生成的 size 符号地址转换为 artifact 字节数。
 *
 * objcopy 的 `_binary_xxx_size` 是绝对符号而非数组，取其地址转整数即字节大小。
 */
static uint32_t snn_head_binary_size(const uint8_t *size_symbol)
{
    return (uint32_t)(uintptr_t)size_symbol;
}

bool snn_head_read_layer_artifact(const uint8_t *artifact_start,
                                  const uint8_t *artifact_size_symbol,
                                  snn_head_layer_artifact_context_t *context)
{
    if ((artifact_start == NULL) || (artifact_size_symbol == NULL) ||
        (context == NULL)) {
        return false;
    }

    if (rvrt_artifact_read(artifact_start,
                           snn_head_binary_size(artifact_size_symbol),
                           &context->artifact) != RVRT_ARTIFACT_OK) {
        return false;
    }
    if (rvrt_artifact_thread_runtime(&context->artifact, 0U,
                                     &context->runtime) != RVRT_ARTIFACT_OK) {
        return false;
    }
    if (rvrt_artifact_get_input_mapping_view(&context->artifact, 0U, 0U,
                                             &context->input_view) !=
        RVRT_ARTIFACT_OK) {
        return false;
    }
    if (rvrt_artifact_get_output_mapping_view(&context->artifact, 0U, 0U,
                                              &context->output_view) !=
        RVRT_ARTIFACT_OK) {
        return false;
    }

    return true;
}

bool snn_head_validate_layer_artifact(
    const snn_head_layer_artifact_context_t *context,
    const snn_head_layer_artifact_contract_t *contract)
{
    if ((context == NULL) || (contract == NULL)) {
        return false;
    }

    /* 确认是单步 artifact（CPU 外层循环负责跑完整 8-step chunk）。 */
    if ((context->runtime.timesteps != SNN_HEAD_ARTIFACT_TIMESTEPS) ||
        (context->runtime.tick_depth != SNN_HEAD_ARTIFACT_TICK_DEPTH) ||
        (context->runtime.sync_steps != SNN_HEAD_ARTIFACT_TIMESTEPS) ||
        (context->runtime.decode_mode != RVRT_DECODE_MODE_STREAM)) {
        return false;
    }

    /* 确认输入 mapping 的 entry 数和 int8 宽度。 */
    if ((context->input_view.entry_count != contract->input_entries) ||
        (context->input_view.bit_width != contract->input_bit_width)) {
        return false;
    }

    /* 确认输出 mapping 的帧条目数、逻辑元素数、kind 和 dtype。 */
    if ((context->output_view.entry_count != contract->output_entries) ||
        (context->output_view.element_count != contract->output_elements) ||
        (context->output_view.kind != contract->output_kind) ||
        (context->output_view.dtype != contract->output_dtype)) {
        return false;
    }

    return true;
}

bool snn_head_run_chunk(const float *input, float *action)
{
    if ((input == NULL) || (action == NULL)) {
        return false;
    }

    /* 按层串联；各层直接引用文件级共享 buffer，中间结果在 tensor_workspace 覆盖推进。 */
    if (!snn_head_run_fc1_lif(input)) {
        return false;
    }
    if (!snn_head_run_block_lif(
            snn_head_block0_ln_weight, snn_head_block0_ln_bias,
            snn_head_block0_activation_scale,
            snn_head_block0_lif_artifact_start,
            snn_head_block0_lif_artifact_size,
            &SNN_HEAD_BLOCK_LIF_CONTRACT)) {
        return false;
    }
    if (!snn_head_run_block_lif(
            snn_head_block1_ln_weight, snn_head_block1_ln_bias,
            snn_head_block1_activation_scale,
            snn_head_block1_lif_artifact_start,
            snn_head_block1_lif_artifact_size,
            &SNN_HEAD_BLOCK_LIF_CONTRACT)) {
        return false;
    }
    if (!snn_head_run_fc2()) {
        return false;
    }
    if (!snn_head_run_fc3(action)) {
        return false;
    }

    return true;
}
