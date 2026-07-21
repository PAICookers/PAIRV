#include "snn_head_internal.h"

/* fc3：int8[8][1536] 输入、acc_int32[8][7] 输出（动作前累加器，VOLTAGE/INT32，
 * 元素数仅 7）。仅本层引用。 */
static const snn_head_layer_artifact_contract_t SNN_HEAD_FC3_CONTRACT = {
    SNN_HEAD_HIDDEN_DIM,
    8U,
    SNN_HEAD_ACTION_DIM,
    SNN_HEAD_ACTION_DIM,
    RVRT_OUTPUT_VOLTAGE,
    SNN_HEAD_DTYPE_INT32,
};

/**
 * @brief 执行 fc3 层：量化 -> fc3 线性 -> acc_int32 -> 反量化到 action。
 *
 * 最终读出层，前无 LayerNorm：入口 tensor_workspace 为 fc2 反量化后的 li_out 膜电位
 * float32[8][1536]，直接量化后送 PAICore。输出 acc_int32[8][7] 同为 VOLTAGE/INT32，
 * 复用 fc2_voltage_state 的前 7 个 slot 逐帧拼接；acc 仅 224 字节直接放栈上。
 *
 * @param action 输出动作缓冲区，形状 float32[8][7]，由本函数写入最终结果。
 * @return 成功返回 true，否则返回 false。
 */
bool snn_head_run_fc3(float *action)
{
    /* 入口为 fc2 输出的 li_out 膜电位 float32[8][1536]，直接前向原地量化为 int8。 */
    rv_quantize_s8((const float *)tensor_workspace, (int8_t *)tensor_workspace,
                   SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM,
                   snn_head_fc3_activation_scale);
    int8_t *fc3_input_s8 = (int8_t *)tensor_workspace;

    SNN_HEAD_DUMP("fc3.q_int8", tensor_workspace,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM, SNN_HEAD_DUMP_S8);

    /* 读取并校验 fc3 artifact（VOLTAGE/INT32，输出 7 元素）。 */
    snn_head_layer_artifact_context_t fc3_artifact = {0};
    if (!snn_head_read_layer_artifact(snn_head_fc3_artifact_start,
                                      snn_head_fc3_artifact_size,
                                      &fc3_artifact)) {
        return false;
    }
    if (!snn_head_validate_layer_artifact(&fc3_artifact,
                                          &SNN_HEAD_FC3_CONTRACT)) {
        return false;
    }

    /* 建立 fc3 session，共享帧 buffer 兼作 RX。 */
    rvrt_session_t fc3_session = {0};
    const rvrt_session_config_t fc3_session_config = {
        .artifact = &fc3_artifact.artifact,
        .thread_index = 0U,
        .rx_frames = layer_frame_buf,
        .rx_capacity = SNN_HEAD_FRAME_BUF_FRAMES,
    };
    if (rvrt_session_init(&fc3_session, &fc3_session_config) != RVRT_SESSION_OK) {
        return false;
    }
    if (rvrt_session_load_config(&fc3_session) != RVRT_SESSION_OK) {
        return false;
    }

    /* chunk 开头复位一次（chunk 内 8 轮不复位）。 */
    if (rvrt_session_reset_model(&fc3_session, SNN_HEAD_TIMEOUT_MS) !=
        RVRT_SESSION_OK) {
        return false;
    }

    /* acc_int32[8][7] 仅 224 字节，放栈上；解码填满后统一反量化到 action。 */
    int32_t fc3_acc[SNN_HEAD_TIMESTEPS][SNN_HEAD_ACTION_DIM];

    /* 8 个 action timestep 循环，每轮 send -> sync -> 逐帧 voltage 解码。 */
    for (uint32_t timestep = 0U; timestep < SNN_HEAD_TIMESTEPS; ++timestep) {
        const int8_t *const fc3_input_t =
            &fc3_input_s8[timestep * SNN_HEAD_HIDDEN_DIM];
        if (rvrt_session_send_input_timestep(
                &fc3_session, &fc3_artifact.input_view, 0U,
                (const uint8_t *)fc3_input_t, SNN_HEAD_HIDDEN_DIM,
                layer_frame_buf, RVRT_MAX_WORKSPACE_FRAMES) != RVRT_SESSION_OK) {
            return false;
        }

        const rvrt_frame_t *fc3_received_frames = NULL;
        uint32_t fc3_received_count = 0U;
        if (rvrt_session_sync_wait(&fc3_session, fc3_artifact.runtime.tick_depth,
                                   SNN_HEAD_TIMEOUT_MS, &fc3_received_frames,
                                   &fc3_received_count) != RVRT_SESSION_OK) {
            return false;
        }

        /* voltage 解码不自动清零；先清零本行 acc 和 lane 拼接状态（各 7 个）。 */
        int32_t *const fc3_acc_t = &fc3_acc[timestep][0];
        memset(fc3_acc_t, 0, SNN_HEAD_ACTION_DIM * sizeof(fc3_acc_t[0]));
        memset(fc2_voltage_state, 0,
               SNN_HEAD_ACTION_DIM * sizeof(fc2_voltage_state[0]));

        /* 逐帧拼接：凑齐一个 int32 的 4 个 lane 就写入对应槽位。 */
        for (uint32_t frame_idx = 0U; frame_idx < fc3_received_count;
             ++frame_idx) {
            bool written = false;
            if (rvrt_decode_voltage_frame(
                    &fc3_artifact.output_view, &fc3_received_frames[frame_idx],
                    fc3_acc_t, SNN_HEAD_ACTION_DIM, fc2_voltage_state,
                    SNN_HEAD_ACTION_DIM, &written) != RVRT_STATUS_OK) {
                return false;
            }
        }
    }

    SNN_HEAD_DUMP("fc3.paicore_acc", fc3_acc,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_ACTION_DIM, SNN_HEAD_DUMP_S32);

    /* 按 per-channel output_scale 把 acc_int32[8][7] 反量化为最终 action[8][7]。 */
    rv_dequantize_s32(&fc3_acc[0][0], action, SNN_HEAD_TIMESTEPS,
                      SNN_HEAD_ACTION_DIM, snn_head_fc3_output_scale);

    SNN_HEAD_DUMP("fc3.out", action,
                  SNN_HEAD_TIMESTEPS * SNN_HEAD_ACTION_DIM, SNN_HEAD_DUMP_F32);

    return true;
}
