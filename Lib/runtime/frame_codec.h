#ifndef RVRT_FRAME_CODEC_H
#define RVRT_FRAME_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#include "artifact_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of frame construction, input encoding, or output decoding. */
typedef enum rvrt_status_e {
    RVRT_STATUS_OK = 0,
    RVRT_STATUS_DONE = 1,
    RVRT_STATUS_BUFFER_FULL = 2,
    RVRT_STATUS_NULL_ARGUMENT = -1,
    RVRT_STATUS_ARTIFACT_ERROR = -2,
    RVRT_STATUS_OUT_OF_RANGE = -3,
    RVRT_STATUS_BAD_VALUE = -4,
    RVRT_STATUS_UNSUPPORTED = -5,
} rvrt_status_t;

/** @brief One logical 64-bit NoC frame split for the FIFO interface. */
typedef struct rvrt_frame_s {
    uint32_t high;
    uint32_t low;
} rvrt_frame_t;

#define RVRT_FRAME_TYPE_OFFSET 30U
#define RVRT_FRAME_TYPE_WORK 2U
#define RVRT_FRAME_WORK_KIND_OFFSET 29U
#define RVRT_FRAME_WORK_KIND_DATA 0U
#define RVRT_FRAME_WORK_KIND_VOLTAGE 1U
#define RVRT_FRAME_KIND_OFFSET 28U
#define RVRT_FRAME_KIND_COMPLETE 0xEU

#define RVRT_OUTPUT_DATA 0U
#define RVRT_OUTPUT_VOLTAGE 1U

/** @brief Return true when frame has the PAICORE work-frame type tag. */
static inline bool rvrt_frame_is_work(const rvrt_frame_t *frame)
{
    return (frame != NULL) && (((frame->high >> RVRT_FRAME_TYPE_OFFSET) &
                                0x3U) == RVRT_FRAME_TYPE_WORK);
}

/** @brief Return true when frame is a work-frame type 1 DATA frame. */
static inline bool rvrt_frame_is_work_type1(const rvrt_frame_t *frame)
{
    return rvrt_frame_is_work(frame) &&
           (((frame->high >> RVRT_FRAME_WORK_KIND_OFFSET) & 0x1U) ==
            RVRT_FRAME_WORK_KIND_DATA);
}

/** @brief Return true when frame is a work-frame type 2 membrane-voltage frame. */
static inline bool rvrt_frame_is_work_type2(const rvrt_frame_t *frame)
{
    return rvrt_frame_is_work(frame) &&
           (((frame->high >> RVRT_FRAME_WORK_KIND_OFFSET) & 0x1U) ==
            RVRT_FRAME_WORK_KIND_VOLTAGE);
}

/** @brief Return true when frame marks completion of the current PAICORE pass.
 */
static inline bool rvrt_frame_is_complete(const rvrt_frame_t *frame)
{
    return (frame != NULL) && (((frame->high >> RVRT_FRAME_KIND_OFFSET) &
                                0xFU) == RVRT_FRAME_KIND_COMPLETE);
}

/** @brief Resumable position while one input mapping is emitted in chunks. */
typedef struct rvrt_input_cursor_s {
    uint32_t entry_index;
    uint32_t timestep;
} rvrt_input_cursor_t;

/**
 * @brief Build the initialization control frame for an artifact thread.
 * @param artifact Verified artifact providing the thread root address.
 * @param thread_index Artifact thread addressed by the control frame.
 * @param frame Receives the logical high/low frame words.
 */
rvrt_status_t rvrt_build_init_frame(const rvrt_artifact_t *artifact,
                                    uint32_t thread_index, rvrt_frame_t *frame);

/**
 * @brief Build the artifact-default synchronization control frame.
 * @param artifact Verified artifact providing thread runtime metadata.
 * @param thread_index Artifact thread addressed by the control frame.
 * @param frame Receives the logical high/low frame words.
 */
rvrt_status_t rvrt_build_sync_frame(const rvrt_artifact_t *artifact,
                                    uint32_t thread_index, rvrt_frame_t *frame);

/**
 * @brief Reset a cursor before encoding input at the given runtime timestep.
 * @param cursor Cursor reset to the first mapping entry.
 * @param timestep Runtime timestep encoded into generated work frames.
 */
void rvrt_input_cursor_init(rvrt_input_cursor_t *cursor, uint32_t timestep);

/**
 * @brief Encode as many nonzero input entries as fit in frames.
 *
 * Returns DONE when cursor reaches the end, or BUFFER_FULL after preserving
 * the first unencoded entry for the next call.
 * @param view Borrowed input mapping to encode.
 * @param cursor In/out resumable mapping position.
 * @param input Contiguous logical input bytes.
 * @param input_size Logical byte length of input.
 * @param frames Caller workspace for encoded frames.
 * @param frame_capacity Number of frames that fit in frames.
 * @param frame_count Receives frames produced by this call.
 */
rvrt_status_t
rvrt_encode_input_chunk(const rvrt_artifact_input_mapping_view_t *view,
                        rvrt_input_cursor_t *cursor, const uint8_t *input,
                        uint32_t input_size, rvrt_frame_t *frames,
                        uint32_t frame_capacity, uint32_t *frame_count);

/**
 * @brief Decode one received work frame into an output tensor.
 *
 * Returns OK with written set to false for non-work, other-timestep, or
 * unmapped frames; these are valid observations while draining a phase FIFO.
 * @param view Borrowed output mapping used to identify the frame address.
 * @param frame Received logical high/low frame words.
 * @param output Contiguous logical output buffer.
 * @param output_size Logical byte length of output.
 * @param written Receives whether output was modified.
 */
rvrt_status_t
rvrt_decode_output_frame(const rvrt_artifact_output_mapping_view_t *view,
                         const rvrt_frame_t *frame, uint8_t *output,
                         uint32_t output_size, bool *written);

typedef struct rvrt_membrane_decode_state_s {
    uint32_t value;
    uint8_t parts_received;
} rvrt_membrane_decode_state_t;

/**
 * @brief Decode one work-frame type 2 membrane-voltage data part.
 *
 * One membrane voltage is delivered as four 8-bit parts in LSB-to-MSB order.
 * This function keeps per-output-element accumulation in state, so frame parts
 * from different neurons may be interleaved as long as each neuron is locally
 * ordered. written becomes true only when a full int32 value is completed.
 * @param view Borrowed output mapping used to identify the frame address.
 * @param frame Received logical high/low frame words.
 * @param output Contiguous int32 membrane output buffer.
 * @param output_size Number of int32 elements in output.
 * @param state Per-output-element accumulation state.
 * @param state_size Number of entries in state.
 * @param written Receives whether output was modified.
 */
rvrt_status_t rvrt_decode_membrane_frame(
    const rvrt_artifact_output_mapping_view_t *view, const rvrt_frame_t *frame,
    int32_t *output, uint32_t output_size,
    rvrt_membrane_decode_state_t *state, uint32_t state_size, bool *written);

const char *rvrt_status_string(rvrt_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_FRAME_CODEC_H */
