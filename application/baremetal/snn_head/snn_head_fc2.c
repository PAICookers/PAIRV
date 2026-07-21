#include "snn_head_internal.h"

/* fc2：int8[8][1536] 输入、li_out 膜电位 int32[8][1536] 输出（VOLTAGE/INT32，
 * 每元素 4 个 lane 帧），与前三层的 DATA/UINT1 不同。仅本层引用。 */
static const snn_head_layer_artifact_contract_t SNN_HEAD_FC2_CONTRACT = {
    SNN_HEAD_HIDDEN_DIM,
    8U,
    SNN_HEAD_HIDDEN_DIM,
    SNN_HEAD_HIDDEN_DIM,
    RVRT_OUTPUT_VOLTAGE,
    SNN_HEAD_DTYPE_INT32,
};

/**
 * @brief 执行 fc2 层：LN2 -> 量化 -> fc2 线性 -> li_out 膜电位。
 *
 * VOLTAGE/INT32 路径：PAICore 输出 int32 膜电位，每个 int32 拆 4 个 lane 帧传回，
 * 需逐帧用 rvrt_decode_voltage_frame 拼接（无批量解码 API）。li_out 复位机制为 none，
 * 膜电位在 chunk 内跨 timestep 演化，故仅 chunk 开头复位一次。int32 输出比 int8 输入宽
 * 4 倍，先把 int8 反向扩排成 stride=6144，让每行输出原地覆盖同行已消费的输入而不踩下一行。
 *
 * @return 成功返回 true；完成后 tensor_workspace 开头为 float32[8][1536]
 *         （li_out 膜电位反量化结果），作为 fc3 的输入。
 */
bool snn_head_run_fc2(void)
{
    float *workspace_f32 = (float *)tensor_workspace;

    /* LN2(1536) 原地计算。 */
    rv_layernorm_f32(workspace_f32, workspace_f32, SNN_HEAD_TIMESTEPS,
                     SNN_HEAD_HIDDEN_DIM, snn_head_ln2_weight, snn_head_ln2_bias,
                     RV_LAYERNORM_DEFAULT_EPS);

    SNN_HEAD_DUMP("fc2.ln", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_F32);

    /* LN2 输出原地量化为 int8。 */
    rv_quantize_s8((const float *)tensor_workspace, (int8_t *)tensor_workspace,
                   SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM,
                   snn_head_fc2_activation_scale);

    SNN_HEAD_DUMP("fc2.q_int8", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_S8);

    /* int8 从紧凑 stride=1536 反向扩排成 stride=6144（行内后 4608 字节留给本行 int32
     * 输出）；反向遍历目的偏移恒 >= 源偏移，不覆盖尚未搬运的低行输入。 */
    int8_t *fc2_input_s8 = (int8_t *)tensor_workspace;
    for (uint32_t timestep = SNN_HEAD_TIMESTEPS; timestep > 0U; --timestep) {
        const uint32_t row = timestep - 1U;
        memmove(&fc2_input_s8[row * SNN_HEAD_VOLTAGE_STRIDE_BYTES],
                &fc2_input_s8[row * SNN_HEAD_HIDDEN_DIM], SNN_HEAD_HIDDEN_DIM);
    }

    /* 读取并校验 fc2 artifact（VOLTAGE/INT32）。 */
    snn_head_layer_artifact_context_t fc2_artifact = {0};
    if (!snn_head_read_layer_artifact(snn_head_fc2_artifact_start,
                                      snn_head_fc2_artifact_size,
                                      &fc2_artifact)) {
        return false;
    }
    if (!snn_head_validate_layer_artifact(&fc2_artifact,
                                          &SNN_HEAD_FC2_CONTRACT)) {
        return false;
    }

    /* 建立 fc2 session，共享帧 buffer 兼作 RX（VOLTAGE 需 6145 帧容量）。 */
    rvrt_session_t fc2_session = {0};
    const rvrt_session_config_t fc2_session_config = {
        .artifact = &fc2_artifact.artifact,
        .thread_index = 0U,
        .rx_frames = layer_frame_buf,
        .rx_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
    };
    if (rvrt_session_init(&fc2_session, &fc2_session_config) != RVRT_SESSION_OK) {
        return false;
    }
    if (rvrt_session_load_config(&fc2_session) != RVRT_SESSION_OK) {
        return false;
    }

    /* chunk 开头复位一次 li_out 膜电位残留（chunk 内 8 轮不复位）。 */
    if (rvrt_session_reset_model(&fc2_session, SNN_HEAD_TIMEOUT_MS) !=
        RVRT_SESSION_OK) {
        return false;
    }

    /* 8 个 action timestep 循环，每轮 send -> sync -> 逐帧 voltage 解码。 */
    for (uint32_t timestep = 0U; timestep < SNN_HEAD_TIMESTEPS; ++timestep) {
        const int8_t *const fc2_input_t =
            &fc2_input_s8[timestep * SNN_HEAD_VOLTAGE_STRIDE_BYTES];
        if (rvrt_session_send_input_timestep(
                &fc2_session, &fc2_artifact.input_view, 0U,
                (const uint8_t *)fc2_input_t, SNN_HEAD_HIDDEN_DIM,
                layer_frame_buf, RVRT_MAX_WORKSPACE_FRAMES) != RVRT_SESSION_OK) {
            return false;
        }

        const rvrt_frame_t *fc2_received_frames = NULL;
        uint32_t fc2_received_count = 0U;
        if (rvrt_session_sync_wait(&fc2_session, fc2_artifact.runtime.tick_depth,
                                   SNN_HEAD_TIMEOUT_MS, &fc2_received_frames,
                                   &fc2_received_count) != RVRT_SESSION_OK) {
            return false;
        }

        /* int32 膜电位原地写回本行 6144 字节。voltage 解码不自动清零，先清零本行和
         * lane 拼接状态，未收齐 4 lane 的元素保持 0。 */
        int32_t *const fc2_voltage_t =
            (int32_t *)&fc2_input_s8[timestep * SNN_HEAD_VOLTAGE_STRIDE_BYTES];
        memset(fc2_voltage_t, 0, SNN_HEAD_VOLTAGE_STRIDE_BYTES);
        memset(fc2_voltage_state, 0, sizeof(fc2_voltage_state));

        /* 逐帧拼接：凑齐一个 int32 的 4 个 lane 就写入对应槽位。 */
        for (uint32_t frame_idx = 0U; frame_idx < fc2_received_count;
             ++frame_idx) {
            bool written = false;
            if (rvrt_decode_voltage_frame(
                    &fc2_artifact.output_view, &fc2_received_frames[frame_idx],
                    fc2_voltage_t, SNN_HEAD_HIDDEN_DIM, fc2_voltage_state,
                    SNN_HEAD_HIDDEN_DIM, &written) != RVRT_STATUS_OK) {
                return false;
            }
        }
    }

    SNN_HEAD_DUMP("fc2.paicore_v", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_S32);

    /* 8 行 int32 膜电位已是连续 int32[8][1536]，按 per-channel output_scale 原地
     * 反量化为 float32[8][1536]，作为 fc3 的浮点输入。 */
    rv_dequantize_s32((const int32_t *)tensor_workspace, (float *)tensor_workspace,
                      SNN_HEAD_TIMESTEPS, SNN_HEAD_HIDDEN_DIM,
                      snn_head_fc2_output_scale);

    SNN_HEAD_DUMP("fc2.out", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_F32);

    return true;
}
