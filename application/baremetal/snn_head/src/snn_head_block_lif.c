#include "snn_head_internal.h"

/* block0/block1：int8[8][1536] 输入、spike[8][1536] 输出，两 block
 * 拓扑同构共用同一契约。 由 run_chunk 传入
 * snn_head_run_block_lif，故跨文件可见（非 static）。 */
const snn_head_layer_artifact_contract_t SNN_HEAD_BLOCK_LIF_CONTRACT = {
    SNN_HEAD_HIDDEN_DIM, 8U,
    SNN_HEAD_HIDDEN_DIM, SNN_HEAD_HIDDEN_DIM,
    RVRT_OUTPUT_DATA,    SNN_HEAD_DTYPE_UINT1,
};

/**
 * @brief 执行一个 block（block0 或 block1）：LN(1536) -> 量化 -> fc+LIF ->
 * spike。
 *
 * 两 block 仅 LN 仿射、activation_scale 和 artifact
 * 不同，故参数化为一个函数调用两次。
 *
 * @param ln_weight LN(1536) 的 gamma，长度 SNN_HEAD_HIDDEN_DIM。
 * @param ln_bias LN(1536) 的 beta，长度 SNN_HEAD_HIDDEN_DIM。
 * @param activation_scale 当前 block 的对称量化 activation_scale。
 * @param artifact_start 当前 block artifact 二进制起始地址。
 * @param artifact_size_symbol objcopy 生成的 size 绝对符号地址。
 * @param contract 当前 block artifact 必须满足的静态契约。
 * @return 成功返回 true；完成后 tensor_workspace 开头为 float32[8][1536]（spike
 * 已拓宽）。
 */
bool snn_head_run_block_lif(const float *ln_weight, const float *ln_bias,
                            snn_head_layer_id_t layer, float activation_scale,
                            const uint8_t *artifact_start,
                            const uint8_t *artifact_size_symbol,
                            const snn_head_layer_artifact_contract_t *contract)
{
    /* 入口 tensor_workspace 为上一层拓宽后的 float32[8][1536]。 */
    float *workspace_f32 = (float *)tensor_workspace;

    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_CPU_PREPROCESS);
    /* LN(1536) 原地计算。 */
    rv_layernorm_f32(workspace_f32, workspace_f32, SNN_HEAD_TIMESTEPS,
                     SNN_HEAD_HIDDEN_DIM, ln_weight, ln_bias,
                     RV_LAYERNORM_DEFAULT_EPS);

    SNN_HEAD_DUMP("block.ln", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_F32);

    /* LN 输出原地量化为 int8（缩小写入，前向原地安全）。 */
    rv_quantize_s8((const float *)tensor_workspace, (int8_t *)tensor_workspace,
                   SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, activation_scale);

    SNN_HEAD_DUMP("block.q_int8", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_S8);

    /* 输入维 == hidden == 1536，行已满宽，无需扩排，输出可直接覆盖当前行。 */
    int8_t *block_input_s8 = (int8_t *)tensor_workspace;
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_CPU_PREPROCESS);

    /* 读取并校验当前 block artifact。 */
    snn_head_layer_artifact_context_t block_artifact = {0};
    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_ARTIFACT_VALIDATE);
    const bool artifact_ok =
        snn_head_read_layer_artifact(artifact_start, artifact_size_symbol,
                                     &block_artifact) &&
        snn_head_validate_layer_artifact(&block_artifact, contract);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_ARTIFACT_VALIDATE);
    if (!artifact_ok) {
        return false;
    }

    const rvrt_paicore_runner_deploy_config_t runner_config = {
        .artifact_data = artifact_start,
        .artifact_size = snn_head_artifact_size(artifact_size_symbol),
        .frame_buffer = layer_frame_buf,
        .frame_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
        .timeout_ms = SNN_HEAD_TIMEOUT_MS,
    };
    const char *const layer_name =
        layer == SNN_HEAD_LAYER_BLOCK0 ? "block0" : "block1";
    if (!snn_head_run_paicore_layer(
            layer_name, &runner_config, (const uint8_t *)block_input_s8,
            SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_HIDDEN_DIM,
            (uint8_t *)block_input_s8, SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM,
            SNN_HEAD_HIDDEN_DIM)) {
        return false;
    }
    SNN_HEAD_DUMP("block.paicore_spike", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_U8);

    SNN_HEAD_PROFILE_PHASE_BEGIN(SNN_HEAD_PROFILE_CPU_POSTPROCESS);
    /* spike 字节 uint8[8][1536] 反向原地拓宽为 float32[8][1536]，供下一层 LN
     * 读取。 */
    const uint8_t *block_spike_u8 = (const uint8_t *)tensor_workspace;
    float *block_spike_f32 = (float *)tensor_workspace;
    for (uint32_t idx = SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM; idx > 0U;
         --idx) {
        const uint32_t elem = idx - 1U;
        block_spike_f32[elem] = (float)block_spike_u8[elem];
    }

    SNN_HEAD_DUMP("block.out", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_F32);
    SNN_HEAD_PROFILE_PHASE_END(SNN_HEAD_PROFILE_CPU_POSTPROCESS);

    return true;
}
