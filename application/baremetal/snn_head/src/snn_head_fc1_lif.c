#include "snn_head_internal.h"

/* fc1_lif：int8[8][768] 输入、spike[8][1536] 输出，仅本层引用。 */
static const snn_head_layer_artifact_contract_t SNN_HEAD_FC1_LIF_CONTRACT = {
    SNN_HEAD_INPUT_DIM,  8U,
    SNN_HEAD_HIDDEN_DIM, SNN_HEAD_HIDDEN_DIM,
    RVRT_OUTPUT_DATA,    SNN_HEAD_DTYPE_UINT1,
};

/**
 * @brief 执行 fc1_lif 层：float 输入 -> LN1 -> 量化 -> PAICore -> spike。
 * @param input 外部输入张量，形状 float32[8][768]。
 * @return 成功返回 true；完成后 tensor_workspace 开头为 float32[8][1536]（spike
 * 已拓宽）。
 */
bool snn_head_run_fc1_lif(const float *input)
{
    float *workspace_f32 = (float *)tensor_workspace;

    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_CPU_PREPROCESS);
    /* 拷入外部输入，随后 LN1 原地计算。 */
    memcpy(workspace_f32, input,
           SNN_HEAD_INPUT_FLOAT_COUNT * sizeof(*workspace_f32));
    rv_layernorm_f32(workspace_f32, workspace_f32, SNN_HEAD_TIMESTEPS,
                     SNN_HEAD_INPUT_DIM, snn_head_ln1_weight, snn_head_ln1_bias,
                     RV_LAYERNORM_DEFAULT_EPS);

    SNN_HEAD_DUMP("fc1.ln", tensor_workspace, SNN_HEAD_INPUT_FLOAT_COUNT,
                  SNN_HEAD_DUMP_F32);

    /* LN1 输出原地量化为 int8（缩小写入，前向原地安全）。 */
    rv_quantize_s8((const float *)tensor_workspace, (int8_t *)tensor_workspace,
                   SNN_HEAD_INPUT_FLOAT_COUNT, snn_head_fc1_activation_scale);

    SNN_HEAD_DUMP("fc1.q_int8", tensor_workspace, SNN_HEAD_INPUT_FLOAT_COUNT,
                  SNN_HEAD_DUMP_S8);

    /* 把紧凑 int8[8][768] 反向扩排成 stride=1536，让每个 timestep
     * 的输出能原地覆盖 本行，而不踩到后续尚未发送的 timestep 输入。 */
    int8_t *fc1_input_s8 = (int8_t *)tensor_workspace;
    for (uint32_t timestep = SNN_HEAD_TIMESTEPS; timestep > 0U; --timestep) {
        const uint32_t row = timestep - 1U;
        memmove(&fc1_input_s8[row * SNN_HEAD_HIDDEN_DIM],
                &fc1_input_s8[row * SNN_HEAD_INPUT_DIM], SNN_HEAD_INPUT_DIM);
    }
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_CPU_PREPROCESS);

    /* 读取并校验 fc1_lif artifact。 */
    snn_head_layer_artifact_context_t fc1_lif_artifact = {0};
    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_ARTIFACT_VALIDATE);
    const bool artifact_ok =
        snn_head_read_layer_artifact(snn_head_fc1_lif_artifact_start,
                                     snn_head_fc1_lif_artifact_size,
                                     &fc1_lif_artifact) &&
        snn_head_validate_layer_artifact(&fc1_lif_artifact,
                                         &SNN_HEAD_FC1_LIF_CONTRACT);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_ARTIFACT_VALIDATE);
    if (!artifact_ok) {
        return false;
    }

    const rvrt_paicore_runner_deploy_config_t runner_config = {
        .artifact_data = snn_head_fc1_lif_artifact_start,
        .artifact_size = snn_head_artifact_size(snn_head_fc1_lif_artifact_size),
        .frame_buffer = layer_frame_buf,
        .frame_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
        .timeout_ms = SNN_HEAD_TIMEOUT_MS,
    };
    /* runner 负责 reset once、发送完整输入窗口、completion sync 和输出拼接。 */
    if (!snn_head_run_paicore_layer(
            "fc1", &runner_config, (const uint8_t *)fc1_input_s8,
            SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_HIDDEN_DIM,
            (uint8_t *)fc1_input_s8, SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM,
            SNN_HEAD_HIDDEN_DIM)) {
        return false;
    }

    SNN_HEAD_DUMP("fc1.paicore_spike", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_U8);

    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_CPU_POSTPROCESS);
    /* 把 spike 字节 uint8[8][1536] 反向原地拓宽为 float32[8][1536]：float 槽
     * elem 占 [4*elem,4*elem+4)，恒在源字节之后，不覆盖尚未读取的低位字节。 */
    const uint8_t *fc1_spike_u8 = (const uint8_t *)tensor_workspace;
    float *fc1_spike_f32 = (float *)tensor_workspace;
    for (uint32_t idx = SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM; idx > 0U;
         --idx) {
        const uint32_t elem = idx - 1U;
        fc1_spike_f32[elem] = (float)fc1_spike_u8[elem];
    }

    SNN_HEAD_DUMP("fc1.out", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_F32);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_CPU_POSTPROCESS);

    return true;
}
