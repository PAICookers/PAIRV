#ifndef RVRT_FRAME_CODEC_INTERNAL_H
#define RVRT_FRAME_CODEC_INTERNAL_H

#include "frame_codec.h"

#define RVRT_WORK_FRAME_TARGET_LCN_MAX 7U
#define RVRT_WORK_FRAME_TS_HI_WORD_OFFSET 28U
#define RVRT_WORK_FRAME_TS_LO_OFFSET 17U
#define RVRT_WORK_FRAME_TS_LO_MASK 0x7FU
#define RVRT_WORK_FRAME_AX_BITS 9U
#define RVRT_WORK_FRAME_AX_OFFSET 8U
#define RVRT_WORK_FRAME_AX_MASK 0x1FFU
#define RVRT_WORK_FRAME_PAYLOAD_MASK 0xFFU
#define RVRT_VOLTAGE_LANE_BITS 8U
#define RVRT_VOLTAGE_LANE_COUNT 4U
#define RVRT_VOLTAGE_COMPLETE_MASK ((1U << RVRT_VOLTAGE_LANE_COUNT) - 1U)

static inline rvrt_codec_status_t
rvrt_output_frame_address(const rvrt_artifact_output_mapping_view_t *view,
                          const rvrt_frame_t *frame, uint32_t *timestep,
                          uint32_t *axon_bit_idx)
{
    if ((view == NULL) || (frame == NULL) || (timestep == NULL) ||
        (axon_bit_idx == NULL)) {
        return RVRT_CODEC_STATUS_NULL_ARGUMENT;
    }
    if (view->target_lcn > RVRT_WORK_FRAME_TARGET_LCN_MAX) {
        return RVRT_CODEC_STATUS_UNSUPPORTED;
    }

    const uint32_t encoded_timestep =
        (((frame->high >> RVRT_WORK_FRAME_TS_HI_WORD_OFFSET) & 1U) << 7U) |
        ((frame->low >> RVRT_WORK_FRAME_TS_LO_OFFSET) &
         RVRT_WORK_FRAME_TS_LO_MASK);
    const uint32_t frame_axon =
        (frame->low >> RVRT_WORK_FRAME_AX_OFFSET) & RVRT_WORK_FRAME_AX_MASK;
    *timestep = encoded_timestep >> view->target_lcn;
    *axon_bit_idx =
        ((encoded_timestep << RVRT_WORK_FRAME_AX_BITS) | frame_axon) &
        ((1U << (RVRT_WORK_FRAME_AX_BITS + view->target_lcn)) - 1U);
    return RVRT_CODEC_STATUS_OK;
}

static inline rvrt_codec_status_t
rvrt_store_voltage_lane(int32_t *output, size_t output_index,
                        size_t output_count, rvrt_voltage_decode_state_t *state,
                        size_t state_index, size_t state_count, uint32_t lane,
                        uint8_t payload, bool *written)
{
    if ((output_index >= output_count) || (state_index >= state_count) ||
        (lane >= RVRT_VOLTAGE_LANE_COUNT)) {
        return RVRT_CODEC_STATUS_OUT_OF_RANGE;
    }

    rvrt_voltage_decode_state_t *const slot = &state[state_index];
    const uint8_t lane_mask = (uint8_t)(1U << lane);
    if ((slot->received_mask & lane_mask) != 0U) {
        return RVRT_CODEC_STATUS_BAD_VALUE;
    }
    if (slot->received_mask == 0U) {
        output[output_index] = 0;
    }
    output[output_index] =
        (int32_t)((uint32_t)output[output_index] |
                  ((uint32_t)payload << (lane * RVRT_VOLTAGE_LANE_BITS)));
    slot->received_mask |= lane_mask;
    if (slot->received_mask == RVRT_VOLTAGE_COMPLETE_MASK) {
        slot->received_mask = 0U;
        if (written != NULL) {
            *written = true;
        }
    }
    return RVRT_CODEC_STATUS_OK;
}

/*
 * Private runner support. This is deliberately not part of frame_codec.h:
 * applications either decode a complete DATA window or own a manual protocol
 * schedule, while only paicore_runner needs timestamp-scattered incremental
 * RX. The caller supplies a complete sample window, so frames outside that
 * window are ignored without touching output or VOLTAGE lane state.
 */
rvrt_codec_status_t rvrt_decode_output_frames_incremental(
    const rvrt_artifact_output_mapping_view_t *view, const rvrt_frame_t *frames,
    uint32_t frame_count, uint32_t total_timesteps, uint8_t *output,
    size_t output_size, size_t output_stride,
    rvrt_voltage_decode_state_t *voltage_state,
    uint32_t voltage_state_capacity);

#endif /* RVRT_FRAME_CODEC_INTERNAL_H */
