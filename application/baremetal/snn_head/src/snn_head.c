#include "snn_head.h"
#include "snn_head_internal.h"

/*
 * SNN Head 分层实现的核心文件：三块跨层共享 buffer 的唯一定义、两个公共 helper
 * （读取/校验单层 artifact）、以及公共 API snn_head_run_chunk()
 * （fc1_lif -> block0 -> block1 -> fc2 -> fc3）。各层计算见 snn_head_fc*.c，
 * 宏/参数/artifact 符号/类型/函数声明见 snn_head_internal.h。
 */

/* 主张量 workspace，五层共用，最大 float32/int32[8][1536] = 48KB。底层 uint8_t
 * 字节 缓冲，各层按阶段解释成 float、int8 或 int32，按 stride=1536
 * 行布局原地覆盖推进。 aligned(4) 保证转换成 float* 或 int32_t*
 * 时满足对齐。唯一定义在此（外部链接）。 */
uint8_t tensor_workspace[SNN_HEAD_TENSOR_WORKSPACE_BYTES]
    __attribute__((aligned(4)));

/* 层间共享帧 buffer：输入编码分块 workspace，也是 reset completion
 * 的最小接收存储。 runner 的样本 completion 在 IRQ 中流式解码 work
 * frame，不需要按完整输出窗口预留。 */
rvrt_frame_t layer_frame_buf[SNN_HEAD_FRAME_BUF_FRAMES];

/* fc2 VOLTAGE 解码的逐元素 lane 掩码状态，部分值直接累积到输出；runner 在
 * 每个完整层样本开始时整体清零。DATA 层不用，仅 fc2/fc3 复用。 */
rvrt_voltage_decode_state_t fc2_voltage_state[SNN_HEAD_TIMESTEPS]
                                             [SNN_HEAD_HIDDEN_DIM];

/**
 * @brief 将 objcopy 生成的 size 符号地址转换为 artifact 字节数。
 *
 * objcopy 的 `_binary_xxx_size` 是绝对符号而非数组，取其地址转整数即字节大小。
 */
size_t snn_head_artifact_size(const uint8_t *size_symbol)
{
    return (size_t)(uintptr_t)size_symbol;
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
                           snn_head_artifact_size(artifact_size_symbol),
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

    /* 确认 artifact 与当前 sample-level API 的 8-step control 契约一致。输出
     * mapping 的物理地址只影响 work-frame 解码，不改变 completion target。 */
    if ((context->runtime.timesteps != SNN_HEAD_TIMESTEPS) ||
        (context->runtime.pipeline_latency != SNN_HEAD_ARTIFACT_TICK_DEPTH) ||
        (context->runtime.completion_sync_timestep != SNN_HEAD_TIMESTEPS) ||
        (context->runtime.output_time_encoding !=
         RVRT_OUTPUT_TIME_ENCODING_STREAM)) {
        return false;
    }

    /* 确认输入 mapping 的帧条目、逻辑元素数和 int8 宽度。 */
    if ((context->input_view.entry_count != contract->input_entries) ||
        (context->input_view.element_count != contract->input_entries) ||
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

bool snn_head_run_paicore_layer(
    const char *layer_name, const rvrt_paicore_runner_deploy_config_t *config,
    const uint8_t *input, size_t input_capacity, size_t input_stride,
    void *output, size_t output_capacity, size_t output_stride)
{
    rvrt_paicore_runner_t runner = {0};
    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_DEPLOY);
    const rvrt_session_status_t deploy_status =
        rvrt_paicore_runner_deploy(&runner, config);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_DEPLOY);
    if (!snn_head_runner_status_ok(layer_name, "runner deploy",
                                   deploy_status)) {
        return false;
    }

#if SNN_HEAD_TIMING
    rvrt_session_stats_t stats_after_deploy = {0};
    (void)rvrt_paicore_runner_get_stats(&runner, &stats_after_deploy);
    uint64_t sync_round_trip_cycles[SNN_HEAD_TIMESTEPS] = {0};
    rvrt_paicore_runner_sample_timing_t timing = {
        .sync_round_trip_cycles = sync_round_trip_cycles,
        .sync_round_trip_capacity = SNN_HEAD_TIMESTEPS,
    };
#endif

    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_SAMPLE);
#if SNN_HEAD_TIMING
    const rvrt_session_status_t run_status =
        rvrt_paicore_runner_run_sample_profiled(
            &runner, input, input_capacity, input_stride, output,
            output_capacity, output_stride, &timing);
#else
    const rvrt_session_status_t run_status = rvrt_paicore_runner_run_sample(
        &runner, input, input_capacity, input_stride, output, output_capacity,
        output_stride);
#endif
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_SAMPLE);
    if (!snn_head_runner_status_ok(layer_name, "runner run", run_status)) {
        SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_RELEASE);
        (void)rvrt_paicore_runner_release(&runner);
        SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_RELEASE);
        return false;
    }

#if SNN_HEAD_TIMING
    rvrt_session_stats_t stats_after_run = {0};
    (void)rvrt_paicore_runner_get_stats(&runner, &stats_after_run);
    snn_head_profile_record_runner(
        sync_round_trip_cycles, timing.sync_round_trip_count,
        (uint64_t)timing.init_round_trip_cycles,
        (uint64_t)(stats_after_deploy.config_submit_cycles),
        (uint64_t)(stats_after_run.input_encode_cycles -
                   stats_after_deploy.input_encode_cycles),
        (uint64_t)(stats_after_run.input_submit_cycles -
                   stats_after_deploy.input_submit_cycles),
        (uint64_t)(stats_after_run.sync_wait_cycles -
                   stats_after_deploy.sync_wait_cycles),
        (uint64_t)(stats_after_run.rx_irq_service_cycles -
                   stats_after_deploy.rx_irq_service_cycles),
        stats_after_deploy.config_frames,
        stats_after_run.input_frames - stats_after_deploy.input_frames,
        stats_after_run.output_work_frames -
            stats_after_deploy.output_work_frames,
        stats_after_run.complete_frames - stats_after_deploy.complete_frames,
        stats_after_run.rx_irq_count - stats_after_deploy.rx_irq_count);
#endif

    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_RELEASE);
    const rvrt_session_status_t release_status =
        rvrt_paicore_runner_release(&runner);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_RELEASE);
    return snn_head_runner_status_ok(layer_name, "runner release",
                                     release_status);
}

bool snn_head_run_chunk(const float *input, float *action)
{
    if ((input == NULL) || (action == NULL)) {
        return false;
    }

    SNN_HEAD_PROFILE_RESET();
    /* 按层串联；各层直接引用文件级共享 buffer，中间结果在 tensor_workspace
     * 覆盖推进。 */
    SNN_HEAD_PROFILE_LAYER_BEGIN(SNN_HEAD_LAYER_FC1_LIF);
    if (!snn_head_run_fc1_lif(input)) {
        SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_FC1_LIF);
        return false;
    }
    SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_FC1_LIF);
    SNN_HEAD_PROFILE_LAYER_BEGIN(SNN_HEAD_LAYER_BLOCK0);
    if (!snn_head_run_block_lif(
            snn_head_block0_ln_weight, snn_head_block0_ln_bias,
            SNN_HEAD_LAYER_BLOCK0, snn_head_block0_activation_scale,
            snn_head_block0_lif_artifact_start,
            snn_head_block0_lif_artifact_size, &SNN_HEAD_BLOCK_LIF_CONTRACT)) {
        SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_BLOCK0);
        return false;
    }
    SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_BLOCK0);
    SNN_HEAD_PROFILE_LAYER_BEGIN(SNN_HEAD_LAYER_BLOCK1);
    if (!snn_head_run_block_lif(
            snn_head_block1_ln_weight, snn_head_block1_ln_bias,
            SNN_HEAD_LAYER_BLOCK1, snn_head_block1_activation_scale,
            snn_head_block1_lif_artifact_start,
            snn_head_block1_lif_artifact_size, &SNN_HEAD_BLOCK_LIF_CONTRACT)) {
        SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_BLOCK1);
        return false;
    }
    SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_BLOCK1);
    SNN_HEAD_PROFILE_LAYER_BEGIN(SNN_HEAD_LAYER_FC2);
    if (!snn_head_run_fc2()) {
        SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_FC2);
        return false;
    }
    SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_FC2);
    SNN_HEAD_PROFILE_LAYER_BEGIN(SNN_HEAD_LAYER_FC3);
    if (!snn_head_run_fc3(action)) {
        SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_FC3);
        return false;
    }
    SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_FC3);

    return true;
}
