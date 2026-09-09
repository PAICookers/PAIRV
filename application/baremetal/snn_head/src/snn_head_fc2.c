#include "snn_head_internal.h"

/* fc2：int8[8][1536] 输入、裸 Linear 膜电位 int32[8][1536] 输出
 * （VOLTAGE/INT32，每元素 4 个 lane 帧），与前三层的 DATA/UINT1 不同。 */
static const snn_head_layer_artifact_contract_t SNN_HEAD_FC2_CONTRACT = {
    SNN_HEAD_HIDDEN_DIM, 8U,
    SNN_HEAD_HIDDEN_DIM, SNN_HEAD_HIDDEN_DIM,
    RVRT_OUTPUT_VOLTAGE, SNN_HEAD_DTYPE_INT32,
};

/**
 * @brief 执行 fc2 层：LN2 -> 量化 -> fc2 线性膜电位。
 *
 * VOLTAGE/INT32 路径：PAICore 输出 int32 膜电位，每个 int32 拆 4 个 lane
 * 帧传回，由 sample-level runtime helper 逐帧拼接。源模型中的 li_out 在此模型
 * 参数下是数值恒等边界，不参与 PAICORE artifact 编译。int32
 * 输出比 int8 输入宽 4 倍，先把 int8 反向扩排成
 * stride=6144，让每行输出原地覆盖同行已消费的输入而不踩下一行。
 *
 * @return 成功返回 true；完成后 tensor_workspace 开头为 float32[8][1536]
 *         （fc2 膜电位反量化结果），作为 fc3 的输入。
 */
bool snn_head_run_fc2(void)
{
    float *workspace_f32 = (float *)tensor_workspace;

    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_CPU_PREPROCESS);
    /* LN2(1536) 原地计算。 */
    rv_layernorm_f32(workspace_f32, workspace_f32, SNN_HEAD_TIMESTEPS,
                     SNN_HEAD_HIDDEN_DIM, snn_head_ln2_weight,
                     snn_head_ln2_bias, RV_LAYERNORM_DEFAULT_EPS);

    SNN_HEAD_DUMP("fc2.ln", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_F32);

    /* LN2 输出原地量化为 int8。 */
    rv_quantize_s8((const float *)tensor_workspace, (int8_t *)tensor_workspace,
                   SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM,
                   snn_head_fc2_activation_scale);

    SNN_HEAD_DUMP("fc2.q_int8", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_S8);

    /* int8 从紧凑 stride=1536 反向扩排成 stride=6144（行内后 4608 字节留给本行
     * int32 输出）；反向遍历目的偏移恒 >= 源偏移，不覆盖尚未搬运的低行输入。 */
    int8_t *fc2_input_s8 = (int8_t *)tensor_workspace;
    for (uint32_t timestep = SNN_HEAD_TIMESTEPS; timestep > 0U; --timestep) {
        const uint32_t row = timestep - 1U;
        memmove(&fc2_input_s8[row * SNN_HEAD_VOLTAGE_STRIDE_BYTES],
                &fc2_input_s8[row * SNN_HEAD_HIDDEN_DIM], SNN_HEAD_HIDDEN_DIM);
    }
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_CPU_PREPROCESS);

    /* 读取并校验 fc2 artifact（VOLTAGE/INT32）。 */
    snn_head_layer_artifact_context_t fc2_artifact = {0};
    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_ARTIFACT_VALIDATE);
    const bool artifact_ok =
        snn_head_read_layer_artifact(snn_head_fc2_artifact_start,
                                     snn_head_fc2_artifact_size,
                                     &fc2_artifact) &&
        snn_head_validate_layer_artifact(&fc2_artifact, &SNN_HEAD_FC2_CONTRACT);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_ARTIFACT_VALIDATE);
    if (!artifact_ok) {
        return false;
    }

    const rvrt_paicore_runner_deploy_config_t runner_config = {
        .artifact_data = snn_head_fc2_artifact_start,
        .artifact_size = snn_head_artifact_size(snn_head_fc2_artifact_size),
        .frame_buffer = layer_frame_buf,
        .frame_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
        .voltage_state = &fc2_voltage_state[0][0],
        .voltage_state_capacity = SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM,
        .timeout_ms = SNN_HEAD_TIMEOUT_MS,
    };
    if (!snn_head_run_paicore_layer(
            "fc2", &runner_config, (const uint8_t *)fc2_input_s8,
            SNN_HEAD_TIMESTEPS * SNN_HEAD_VOLTAGE_STRIDE_BYTES,
            SNN_HEAD_VOLTAGE_STRIDE_BYTES, (uint8_t *)fc2_input_s8,
            SNN_HEAD_TIMESTEPS * SNN_HEAD_VOLTAGE_STRIDE_BYTES,
            SNN_HEAD_VOLTAGE_STRIDE_BYTES)) {
        return false;
    }
    SNN_HEAD_DUMP("fc2.paicore_v", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_S32);

    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_CPU_POSTPROCESS);
    /* 8 行 int32 膜电位已是连续 int32[8][1536]，按 per-channel output_scale
     * 原地 反量化为 float32[8][1536]，作为 fc3 的浮点输入。 */
    rv_dequantize_s32((const int32_t *)tensor_workspace,
                      (float *)tensor_workspace, SNN_HEAD_TIMESTEPS,
                      SNN_HEAD_HIDDEN_DIM, snn_head_fc2_output_scale);

    SNN_HEAD_DUMP("fc2.out", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_F32);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_CPU_POSTPROCESS);

    return true;
}
