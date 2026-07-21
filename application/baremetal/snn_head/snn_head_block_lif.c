#include "snn_head_internal.h"

/* block0/block1：int8[8][1536] 输入、spike[8][1536] 输出，两 block 拓扑同构共用同一契约。
 * 由 run_chunk 传入 snn_head_run_block_lif，故跨文件可见（非 static）。 */
const snn_head_layer_artifact_contract_t SNN_HEAD_BLOCK_LIF_CONTRACT = {
    SNN_HEAD_HIDDEN_DIM,
    8U,
    SNN_HEAD_HIDDEN_DIM,
    SNN_HEAD_HIDDEN_DIM,
    RVRT_OUTPUT_DATA,
    SNN_HEAD_DTYPE_UINT1,
};

/**
 * @brief 执行一个 block（block0 或 block1）：LN(1536) -> 量化 -> fc+LIF -> spike。
 *
 * 两 block 仅 LN 仿射、activation_scale 和 artifact 不同，故参数化为一个函数调用两次。
 *
 * @param ln_weight LN(1536) 的 gamma，长度 SNN_HEAD_HIDDEN_DIM。
 * @param ln_bias LN(1536) 的 beta，长度 SNN_HEAD_HIDDEN_DIM。
 * @param activation_scale 当前 block 的对称量化 activation_scale。
 * @param artifact_start 当前 block artifact 二进制起始地址。
 * @param artifact_size_symbol objcopy 生成的 size 绝对符号地址。
 * @param contract 当前 block artifact 必须满足的静态契约。
 * @return 成功返回 true；完成后 tensor_workspace 开头为 float32[8][1536]（spike 已拓宽）。
 */
bool snn_head_run_block_lif(
    const float *ln_weight, const float *ln_bias, float activation_scale,
    const uint8_t *artifact_start, const uint8_t *artifact_size_symbol,
    const snn_head_layer_artifact_contract_t *contract)
{
    /* 入口 tensor_workspace 为上一层拓宽后的 float32[8][1536]。 */
    float *workspace_f32 = (float *)tensor_workspace;

    /* LN(1536) 原地计算。 */
    rv_layernorm_f32(workspace_f32, workspace_f32,
                     SNN_HEAD_TIMESTEPS, SNN_HEAD_HIDDEN_DIM,
                     ln_weight, ln_bias, RV_LAYERNORM_DEFAULT_EPS);

    SNN_HEAD_DUMP("block.ln", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_F32);

    /* LN 输出原地量化为 int8（缩小写入，前向原地安全）。 */
    rv_quantize_s8((const float *)tensor_workspace, (int8_t *)tensor_workspace,
                   SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, activation_scale);

    SNN_HEAD_DUMP("block.q_int8", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_S8);

    /* 输入维 == hidden == 1536，行已满宽，无需扩排，输出可直接覆盖当前行。 */
    int8_t *block_input_s8 = (int8_t *)tensor_workspace;

    /* 读取并校验当前 block artifact。 */
    snn_head_layer_artifact_context_t block_artifact = {0};
    if (!snn_head_read_layer_artifact(artifact_start, artifact_size_symbol,
                                      &block_artifact)) {
        return false;
    }
    if (!snn_head_validate_layer_artifact(&block_artifact, contract)) {
        return false;
    }

    /* 建立 session，共享帧 buffer 兼作 RX。 */
    rvrt_session_t block_session = {0};
    const rvrt_session_config_t block_session_config = {
        .artifact = &block_artifact.artifact,
        .thread_index = 0U,
        .rx_frames = layer_frame_buf,
        .rx_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
    };
    if (rvrt_session_init(&block_session, &block_session_config) !=
        RVRT_SESSION_OK) {
        return false;
    }
    if (rvrt_session_load_config(&block_session) != RVRT_SESSION_OK) {
        return false;
    }

    /* chunk 开头复位一次 LIF 膜电位残留；chunk 内 8 轮不复位。 */
    if (rvrt_session_reset_model(&block_session, SNN_HEAD_TIMEOUT_MS) !=
        RVRT_SESSION_OK) {
        return false;
    }

    /* 每个 action timestep 一轮 send -> sync -> 解码直写；application timestep 恒为 0。 */
    for (uint32_t timestep = 0U; timestep < SNN_HEAD_TIMESTEPS; ++timestep) {
        const int8_t *const block_input_t =
            &block_input_s8[timestep * SNN_HEAD_HIDDEN_DIM];
        if (rvrt_session_send_input_timestep(
                &block_session, &block_artifact.input_view, 0U,
                (const uint8_t *)block_input_t, SNN_HEAD_HIDDEN_DIM,
                layer_frame_buf, RVRT_MAX_WORKSPACE_FRAMES) !=
            RVRT_SESSION_OK) {
            return false;
        }

        const rvrt_frame_t *block_received_frames = NULL;
        uint32_t block_received_count = 0U;
        if (rvrt_session_sync_wait(&block_session,
                                   block_artifact.runtime.tick_depth,
                                   SNN_HEAD_TIMEOUT_MS, &block_received_frames,
                                   &block_received_count) != RVRT_SESSION_OK) {
            return false;
        }

        /* spike[1536] 直接解码覆盖当前行。 */
        if (rvrt_decode_output_frames(
                &block_artifact.output_view, &block_artifact.runtime,
                block_received_frames, block_received_count,
                (uint8_t *)&block_input_s8[timestep * SNN_HEAD_HIDDEN_DIM],
                SNN_HEAD_HIDDEN_DIM) != RVRT_STATUS_OK) {
            return false;
        }
    }

    SNN_HEAD_DUMP("block.paicore_spike", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_U8);

    /* spike 字节 uint8[8][1536] 反向原地拓宽为 float32[8][1536]，供下一层 LN 读取。 */
    const uint8_t *block_spike_u8 = (const uint8_t *)tensor_workspace;
    float *block_spike_f32 = (float *)tensor_workspace;
    for (uint32_t idx = SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM; idx > 0U;
         --idx) {
        const uint32_t elem = idx - 1U;
        block_spike_f32[elem] = (float)block_spike_u8[elem];
    }

    SNN_HEAD_DUMP("block.out", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_F32);

    return true;
}
