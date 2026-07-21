#include "snn_head_internal.h"

/* fc1_lif：int8[8][768] 输入、spike[8][1536] 输出，仅本层引用。 */
static const snn_head_layer_artifact_contract_t SNN_HEAD_FC1_LIF_CONTRACT = {
    SNN_HEAD_INPUT_DIM,
    8U,
    SNN_HEAD_HIDDEN_DIM,
    SNN_HEAD_HIDDEN_DIM,
    RVRT_OUTPUT_DATA,
    SNN_HEAD_DTYPE_UINT1,
};

/**
 * @brief 执行 fc1_lif 层：float 输入 -> LN1 -> 量化 -> PAICore -> spike。
 * @param input 外部输入张量，形状 float32[8][768]。
 * @return 成功返回 true；完成后 tensor_workspace 开头为 float32[8][1536]（spike 已拓宽）。
 */
bool snn_head_run_fc1_lif(const float *input)
{
    float *workspace_f32 = (float *)tensor_workspace;

    /* 拷入外部输入，随后 LN1 原地计算。 */
    memcpy(workspace_f32, input, SNN_HEAD_INPUT_BYTES);
    rv_layernorm_f32(workspace_f32, workspace_f32,
                     SNN_HEAD_TIMESTEPS, SNN_HEAD_INPUT_DIM,
                     snn_head_ln1_weight, snn_head_ln1_bias,
                     RV_LAYERNORM_DEFAULT_EPS);

    SNN_HEAD_DUMP("fc1.ln", tensor_workspace, SNN_HEAD_FC1_INPUT_ELEMENTS,
                  SNN_HEAD_DUMP_F32);

    /* LN1 输出原地量化为 int8（缩小写入，前向原地安全）。 */
    rv_quantize_s8((const float *)tensor_workspace,
                   (int8_t *)tensor_workspace,
                   SNN_HEAD_FC1_INPUT_ELEMENTS,
                   snn_head_fc1_activation_scale);

    SNN_HEAD_DUMP("fc1.q_int8", tensor_workspace, SNN_HEAD_FC1_INPUT_ELEMENTS,
                  SNN_HEAD_DUMP_S8);

    /* 把紧凑 int8[8][768] 反向扩排成 stride=1536，让每个 timestep 的输出能原地覆盖
     * 本行，而不踩到后续尚未发送的 timestep 输入。 */
    int8_t *fc1_input_s8 = (int8_t *)tensor_workspace;
    for (uint32_t timestep = SNN_HEAD_TIMESTEPS; timestep > 0U; --timestep) {
        const uint32_t row = timestep - 1U;
        memmove(&fc1_input_s8[row * SNN_HEAD_HIDDEN_DIM],
                &fc1_input_s8[row * SNN_HEAD_INPUT_DIM],
                SNN_HEAD_INPUT_DIM);
    }

    /* 读取并校验 fc1_lif artifact。 */
    snn_head_layer_artifact_context_t fc1_lif_artifact = {0};
    if (!snn_head_read_layer_artifact(snn_head_fc1_lif_artifact_start,
                                      snn_head_fc1_lif_artifact_size,
                                      &fc1_lif_artifact)) {
        return false;
    }
    if (!snn_head_validate_layer_artifact(&fc1_lif_artifact,
                                          &SNN_HEAD_FC1_LIF_CONTRACT)) {
        return false;
    }

    /* 建立 session，layer_frame_buf 兼作 RX buffer。 */
    rvrt_session_t fc1_session = {0};
    const rvrt_session_config_t fc1_session_config = {
        .artifact = &fc1_lif_artifact.artifact,
        .thread_index = 0U,
        .rx_frames = layer_frame_buf,
        .rx_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
    };
    if (rvrt_session_init(&fc1_session, &fc1_session_config) !=
        RVRT_SESSION_OK) {
        return false;
    }
    if (rvrt_session_load_config(&fc1_session) != RVRT_SESSION_OK) {
        return false;
    }

    /* 每个 chunk 开头复位一次 LIF 状态；chunk 内 8 个 timestep 不复位，膜电位延续。 */
    if (rvrt_session_reset_model(&fc1_session, SNN_HEAD_TIMEOUT_MS) !=
        RVRT_SESSION_OK) {
        return false;
    }

    /* 每个 action timestep 一轮；artifact 是单步图，application timestep 恒为 0。 */
    for (uint32_t timestep = 0U; timestep < SNN_HEAD_TIMESTEPS; ++timestep) {
        const int8_t *const fc1_input_t =
            &fc1_input_s8[timestep * SNN_HEAD_HIDDEN_DIM];
        /* 发送上限 RVRT_MAX_WORKSPACE_FRAMES(512)；6145 仅接收侧需要，
         * fc1 最坏 768 帧分两批发完。 */
        if (rvrt_session_send_input_timestep(
                &fc1_session, &fc1_lif_artifact.input_view, 0U,
                (const uint8_t *)fc1_input_t, SNN_HEAD_INPUT_DIM,
                layer_frame_buf, RVRT_MAX_WORKSPACE_FRAMES) !=
            RVRT_SESSION_OK) {
            return false;
        }

        const rvrt_frame_t *fc1_received_frames = NULL;
        uint32_t fc1_received_count = 0U;
        if (rvrt_session_sync_wait(&fc1_session,
                                   fc1_lif_artifact.runtime.tick_depth,
                                   SNN_HEAD_TIMEOUT_MS, &fc1_received_frames,
                                   &fc1_received_count) != RVRT_SESSION_OK) {
            return false;
        }

        /* 解码 spike[1536] 直接覆盖当前行；decode 内部 memset 清本行 1536B，
         * 顺带清零行尾未用的 768B。 */
        if (rvrt_decode_output_frames(
                &fc1_lif_artifact.output_view, &fc1_lif_artifact.runtime,
                fc1_received_frames, fc1_received_count,
                (uint8_t *)&fc1_input_s8[timestep * SNN_HEAD_HIDDEN_DIM],
                SNN_HEAD_HIDDEN_DIM) != RVRT_STATUS_OK) {
            return false;
        }
    }

    SNN_HEAD_DUMP("fc1.paicore_spike", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_U8);

    /* 把 spike 字节 uint8[8][1536] 反向原地拓宽为 float32[8][1536]：float 槽 elem
     * 占 [4*elem,4*elem+4)，恒在源字节之后，不覆盖尚未读取的低位字节。 */
    const uint8_t *fc1_spike_u8 = (const uint8_t *)tensor_workspace;
    float *fc1_spike_f32 = (float *)tensor_workspace;
    for (uint32_t idx = SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM; idx > 0U;
         --idx) {
        const uint32_t elem = idx - 1U;
        fc1_spike_f32[elem] = (float)fc1_spike_u8[elem];
    }

    SNN_HEAD_DUMP("fc1.out", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_F32);

    return true;
}
