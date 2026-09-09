#include "snn_head_internal.h"

/* fc3：int8[8][1536] 输入、acc_int32[8][7] 输出（动作前累加器，VOLTAGE/INT32，
 * 元素数仅 7）。仅本层引用。 */
static const snn_head_layer_artifact_contract_t SNN_HEAD_FC3_CONTRACT = {
    SNN_HEAD_HIDDEN_DIM, 8U,
    SNN_HEAD_ACTION_DIM, SNN_HEAD_ACTION_DIM,
    RVRT_OUTPUT_VOLTAGE, SNN_HEAD_DTYPE_INT32,
};

/**
 * @brief 执行 fc3 层：量化 -> fc3 线性 -> acc_int32 -> 反量化到 action。
 *
 * 最终读出层，前无 LayerNorm：入口 tensor_workspace 为 fc2 反量化后的 li_out
 * 膜电位 float32[8][1536]，直接量化后送 PAICore。输出 acc_int32[8][7] 同为
 * VOLTAGE/INT32， 复用 fc2_voltage_state 的前 7 个元素在全部 timestep 上的 lane
 * state；acc 仅 224 字节直接放栈上。
 *
 * @param action 输出动作缓冲区，形状 float32[8][7]，由本函数写入最终结果。
 * @return 成功返回 true，否则返回 false。
 */
bool snn_head_run_fc3(float *action)
{
    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_CPU_PREPROCESS);
    /* 入口为 fc2 输出的 li_out 膜电位 float32[8][1536]，直接前向原地量化为
     * int8。 */
    rv_quantize_s8((const float *)tensor_workspace, (int8_t *)tensor_workspace,
                   SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM,
                   snn_head_fc3_activation_scale);
    int8_t *fc3_input_s8 = (int8_t *)tensor_workspace;

    SNN_HEAD_DUMP("fc3.q_int8", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_S8);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_CPU_PREPROCESS);

    /* 读取并校验 fc3 artifact（VOLTAGE/INT32，输出 7 元素）。 */
    snn_head_layer_artifact_context_t fc3_artifact = {0};
    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_ARTIFACT_VALIDATE);
    const bool artifact_ok =
        snn_head_read_layer_artifact(snn_head_fc3_artifact_start,
                                     snn_head_fc3_artifact_size,
                                     &fc3_artifact) &&
        snn_head_validate_layer_artifact(&fc3_artifact, &SNN_HEAD_FC3_CONTRACT);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_ARTIFACT_VALIDATE);
    if (!artifact_ok) {
        return false;
    }

    /* acc_int32[8][7] 仅 224 字节，放栈上；解码填满后统一反量化到 action。 */
    int32_t fc3_acc[SNN_HEAD_TIMESTEPS][SNN_HEAD_ACTION_DIM];

    const rvrt_paicore_runner_deploy_config_t runner_config = {
        .artifact_data = snn_head_fc3_artifact_start,
        .artifact_size = snn_head_artifact_size(snn_head_fc3_artifact_size),
        .frame_buffer = layer_frame_buf,
        .frame_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
        .voltage_state = &fc2_voltage_state[0][0],
        .voltage_state_capacity = SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM,
        .timeout_ms = SNN_HEAD_TIMEOUT_MS,
    };
    if (!snn_head_run_paicore_layer(
            "fc3", &runner_config, (const uint8_t *)fc3_input_s8,
            SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_HIDDEN_DIM,
            &fc3_acc[0][0], sizeof(fc3_acc),
            SNN_HEAD_ACTION_DIM * sizeof(int32_t))) {
        return false;
    }
    SNN_HEAD_DUMP("fc3.paicore_acc", fc3_acc,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_ACTION_DIM, SNN_HEAD_DUMP_S32);

    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_CPU_POSTPROCESS);
    /* 按 per-channel output_scale 把 acc_int32[8][7] 反量化为最终
     * action[8][7]。 */
    rv_dequantize_s32(&fc3_acc[0][0], action, SNN_HEAD_TIMESTEPS,
                      SNN_HEAD_ACTION_DIM, snn_head_fc3_output_scale);

    SNN_HEAD_DUMP("fc3.out", action, SNN_HEAD_TIMESTEPS * SNN_HEAD_ACTION_DIM,
                  SNN_HEAD_DUMP_F32);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_CPU_POSTPROCESS);

    return true;
}
