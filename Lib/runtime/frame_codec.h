#ifndef RVRT_FRAME_CODEC_H
#define RVRT_FRAME_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "artifact_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of frame construction, input encoding, or output decoding. */
typedef enum rvrt_status_e {
    /** Frame was handled or intentionally ignored. */
    RVRT_STATUS_OK = 0,
    /** Input cursor reached the final mapping entry. */
    RVRT_STATUS_DONE = 1,
    /** Caller workspace needs another chunk. */
    RVRT_STATUS_BUFFER_FULL = 2,
    /** A required pointer was NULL. */
    RVRT_STATUS_NULL_ARGUMENT = -1,
    /** Artifact mapping lookup failed. */
    RVRT_STATUS_ARTIFACT_ERROR = -2,
    /** Index, capacity, or size is invalid. */
    RVRT_STATUS_OUT_OF_RANGE = -3,
    /** Argument or protocol value is invalid. */
    RVRT_STATUS_BAD_VALUE = -4,
    /** Dtype, output kind, or mode is unsupported. */
    RVRT_STATUS_UNSUPPORTED = -5,
} rvrt_status_t;

/** @brief One logical 64-bit NoC frame split for the FIFO interface. */
typedef struct rvrt_frame_s {
    /** Logical frame bits [63:32], independent of transport word order. */
    uint32_t high;
    /** Logical frame bits [31:0], independent of transport word order. */
    uint32_t low;
} rvrt_frame_t;

/** Bit offset of the two-bit frame type field in rvrt_frame_t.high. */
#define RVRT_FRAME_TYPE_OFFSET 30U
/** Frame type value identifying a PAICORE work frame. */
#define RVRT_FRAME_TYPE_WORK 2U
/** Bit offset of the work-frame DATA/VOLTAGE subtype in high. */
#define RVRT_FRAME_WORK_KIND_OFFSET 29U
/** Work-frame subtype value for DATA payloads. */
#define RVRT_FRAME_WORK_KIND_DATA 0U
/** Work-frame subtype value for VOLTAGE payload lanes. */
#define RVRT_FRAME_WORK_KIND_VOLTAGE 1U
/** Bit offset of the four-bit completion-kind field in high. */
#define RVRT_FRAME_KIND_OFFSET 28U
/** Completion-kind value that ends an armed synchronization phase. */
#define RVRT_FRAME_KIND_COMPLETE 0xEU

/** Output mapping kind value for scalar DATA work frames. */
#define RVRT_OUTPUT_DATA 0U
/** Output mapping kind value for four-lane int32 VOLTAGE frames. */
#define RVRT_OUTPUT_VOLTAGE 1U

/**
 * @brief Return whether a logical frame terminates the current PAICORE pass.
 * @param frame Logical frame to inspect; NULL is treated as false.
 * @return true only for the protocol completion kind.
 */
static inline bool rvrt_frame_is_complete(const rvrt_frame_t *frame)
{
    return (frame != NULL) && (((frame->high >> RVRT_FRAME_KIND_OFFSET) &
                                0xFU) == RVRT_FRAME_KIND_COMPLETE);
}

/**
 * @brief Return whether a logical frame carries a PAICORE work payload.
 * @param frame Logical frame to inspect; NULL is treated as false.
 * @return true for either DATA or VOLTAGE work-frame subtypes.
 */
static inline bool rvrt_frame_is_work(const rvrt_frame_t *frame)
{
    return (frame != NULL) && (((frame->high >> RVRT_FRAME_TYPE_OFFSET) &
                                0x3U) == RVRT_FRAME_TYPE_WORK);
}

/**
 * @brief Resumable state while an input mapping is emitted in bounded chunks.
 *
 * Use rvrt_input_cursor_init() before the first chunk. The convenience input
 * session API owns this state internally; applications only need it when they
 * call rvrt_encode_input_chunk() directly.
 */
typedef struct rvrt_input_cursor_s {
    /** Next input mapping entry to examine. */
    uint32_t entry_index;
    /** Application timestep packed into emitted work frames. */
    uint32_t timestep;
} rvrt_input_cursor_t;

/**
 * @brief Build the initialization control frame for an artifact thread.
 *
 * Standard session use should call rvrt_session_reset_model(), which sends
 * this frame through an armed completion barrier. Use this builder directly
 * only when implementing a custom transport layer.
 * @param artifact Verified artifact providing the thread root address.
 * @param thread_index Zero-based artifact thread index.
 * @param frame Receives the logical high/low frame words; must not be NULL.
 * @return RVRT_STATUS_OK on success; otherwise an argument or artifact status.
 */
rvrt_status_t rvrt_build_init_frame(const rvrt_artifact_t *artifact,
                                    uint32_t thread_index, rvrt_frame_t *frame);

/**
 * @brief Build a synchronization control frame with an explicit step count.
 *
 * Standard session use should call rvrt_session_sync_wait() with the artifact
 * sync_steps rather than transmit this frame manually.
 * @param artifact Verified artifact providing the thread root address.
 * @param thread_index Zero-based artifact thread index.
 * @param sync_steps Synchronization payload; must fit in the control-frame
 *                   24-bit field.
 * @param frame Receives the logical high/low frame words; must not be NULL.
 * @return RVRT_STATUS_OK on success; RVRT_STATUS_BAD_VALUE when sync_steps
 *         exceeds the payload width; or an argument/artifact status.
 */
rvrt_status_t rvrt_build_sync_frame(const rvrt_artifact_t *artifact,
                                    uint32_t thread_index, uint32_t sync_steps,
                                    rvrt_frame_t *frame);

/**
 * @brief Reset a cursor before encoding one application timestep.
 * @param cursor Cursor reset to the first mapping entry; NULL is a no-op.
 * @param timestep Application timestep encoded into generated work frames.
 */
void rvrt_input_cursor_init(rvrt_input_cursor_t *cursor, uint32_t timestep);

/**
 * @brief Encode as many nonzero input entries as fit in frames.
 *
 * Zero input elements advance the cursor but do not emit frames. DONE means
 * the complete mapping has been visited; BUFFER_FULL preserves the first
 * unencoded nonzero entry for the next call.
 * @param view Borrowed input mapping from artifact_reader.
 * @param cursor In/out state initialized for the intended application timestep.
 * @param input Contiguous logical input bytes addressed by mapping elem_idx.
 * @param input_size Number of readable bytes in input.
 * @param frames Caller workspace for encoded logical frames.
 * @param frame_capacity Number of rvrt_frame_t entries in frames; must be
 * nonzero.
 * @param frame_count Receives the number of frames written; must not be NULL.
 * @return RVRT_STATUS_DONE when all entries were visited;
 *         RVRT_STATUS_BUFFER_FULL when the caller must send the current chunk
 *         and call again; or an argument, mapping, range, dtype, or payload
 *         status. frame_count is zeroed before encoding begins.
 */
rvrt_status_t
rvrt_encode_input_chunk(const rvrt_artifact_input_mapping_view_t *view,
                        rvrt_input_cursor_t *cursor, const uint8_t *input,
                        size_t input_size, rvrt_frame_t *frames,
                        uint32_t frame_capacity, uint32_t *frame_count);

/**
 * @brief Decode one received work frame into an output tensor.
 *
 * The compatibility API only writes application timestep zero. Returns OK
 * with written set to false for non-DATA work frames, later timesteps, and
 * unmapped addresses; these are valid observations while draining a phase FIFO.
 * @param view Borrowed output mapping used to identify the frame address.
 * @param frame Received logical high/low frame words.
 * @param output Contiguous logical output buffer indexed by elem_idx.
 * @param output_size Number of writable bytes in output.
 * @param written Receives whether output was modified; must not be NULL.
 * @return RVRT_STATUS_OK for a write or a valid ignored frame;
 *         RVRT_STATUS_UNSUPPORTED for non-DATA output kind/dtype; or an
 *         argument, mapping, or output-range status.
 */
rvrt_status_t
rvrt_decode_output_frame(const rvrt_artifact_output_mapping_view_t *view,
                         const rvrt_frame_t *frame, uint8_t *output,
                         size_t output_size, bool *written);

/**
 * @brief Decode all STREAM DATA frames into application-timestep order.
 *
 * The output layout is [runtime->timesteps][view->element_count]. Frame
 * timestamps outside that application range, non-DATA frames, completion
 * frames, and unmapped addresses are ignored. The required output region is
 * cleared before decoding, so elements without a received spike remain zero.
 *
 * @param view Borrowed DATA output mapping and validated element count.
 * @param runtime Artifact timing and decode-mode metadata for the same thread.
 * @param frames Complete received frame sequence; may be NULL when count is 0.
 * @param frame_count Number of entries in frames.
 * @param output Caller storage for the complete decoded sequence.
 * @param output_size Capacity of output in bytes.
 * @return RVRT_STATUS_OK on success; RVRT_STATUS_UNSUPPORTED for non-STREAM
 *         or non-DATA output; RVRT_STATUS_OUT_OF_RANGE for insufficient output
 *         storage; or a metadata/argument validation status.
 */
rvrt_status_t
rvrt_decode_output_frames(const rvrt_artifact_output_mapping_view_t *view,
                          const rvrt_artifact_runtime_t *runtime,
                          const rvrt_frame_t *frames, uint32_t frame_count,
                          uint8_t *output, size_t output_size);

/**
 * @brief Per-output-element accumulation state for 32-bit voltage decoding.
 *
 * Tracks the partial value and the set of received 8-bit lane indices.
 */
typedef struct rvrt_voltage_decode_state_s {
    /** Received payload byte for each little-endian int32 lane. */
    uint8_t lanes[4];
    /** Bit i is set when lanes[i] has been received for this element. */
    uint8_t received_mask;
} rvrt_voltage_decode_state_t;

/**
 * @brief Decode one work-frame type 2 voltage lane.
 *
 * One voltage is delivered as four address-selected 8-bit lanes. Lanes from
 * different elements may be interleaved and lanes for one element may arrive
 * in any order. Zero-initialize one state entry per output element before the
 * first frame. written becomes true only when all four lanes are present; the
 * state mask is then reset for a potential next value. Like the compatibility
 * DATA decoder, this API accepts application timestep zero only.
 * @param view Borrowed output mapping used to identify the frame address.
 * @param frame Received logical high/low frame words.
 * @param output Contiguous int32 voltage output buffer.
 * @param output_count Number of int32 elements in output.
 * @param state Per-output-element accumulation state.
 * @param state_count Number of entries in state.
 * @param written Receives whether a complete int32 value was written.
 * @return RVRT_STATUS_OK for a completed, partial, or valid ignored frame;
 *         RVRT_STATUS_BAD_VALUE for a duplicate lane; RVRT_STATUS_UNSUPPORTED
 *         for non-VOLTAGE/int32 output; or an argument/mapping/range status.
 */
rvrt_status_t rvrt_decode_voltage_frame(
    const rvrt_artifact_output_mapping_view_t *view, const rvrt_frame_t *frame,
    int32_t *output, uint32_t output_count, rvrt_voltage_decode_state_t *state,
    uint32_t state_count, bool *written);

/**
 * @brief Return a static diagnostic string for a codec status.
 * @param status Status returned by this API family.
 * @return NUL-terminated static string; "unknown" for an unrecognized value.
 *         The caller must not free or modify it.
 */
const char *rvrt_status_string(rvrt_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_FRAME_CODEC_H */
