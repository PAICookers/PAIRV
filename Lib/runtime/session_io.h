#ifndef RVRT_SESSION_IO_H
#define RVRT_SESSION_IO_H

#include <stddef.h>
#include <stdint.h>

#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RVRT_MAX_WORKSPACE_FRAMES
/** Compile-time upper bound for one caller-provided input frame workspace. */
#define RVRT_MAX_WORKSPACE_FRAMES 512U
#endif

#if (RVRT_MAX_WORKSPACE_FRAMES < 1) || (RVRT_MAX_WORKSPACE_FRAMES > 512)
#error "RVRT_MAX_WORKSPACE_FRAMES must be between 1 and 512"
#endif

/**
 * @brief Encode and send one mapped input timestep in bounded chunks.
 *
 * Repeatedly calls the resumable input codec and sends each completed chunk
 * until every mapping entry has been visited. Zero-valued entries advance the
 * internal cursor but do not produce work frames. The helper hides cursor and
 * chunking details while retaining caller control of SRAM use through
 * workspace_capacity.
 *
 * @param session Initialized session used to send encoded frames; its current
 * phase must not be armed.
 * @param mapping Borrowed input mapping describing each tensor element's work
 * frame destination and encoding.
 * @param timestep Application timestep encoded into generated work frames.
 * @param input Contiguous input tensor bytes read according to mapping element
 * indices.
 * @param input_size Number of readable bytes in input.
 * @param workspace Caller-owned storage reused for each encoded frame chunk.
 * @param workspace_capacity Number of frames in workspace; must be in the
 * inclusive range 1..RVRT_MAX_WORKSPACE_FRAMES.
 * @return RVRT_SESSION_OK after the complete mapping has been sent.
 * @return RVRT_SESSION_RUNTIME_ERROR for invalid arguments, invalid workspace
 * capacity, or an input encoding failure.
 * @return Any other status returned by rvrt_session_send_frames().
 *
 * @note The operation is not transactional. Frames sent before a later
 * encoding or transport error are not rolled back.
 */
rvrt_session_status_t rvrt_session_send_input_timestep(
    rvrt_session_t *session, const rvrt_artifact_input_mapping_view_t *mapping,
    uint32_t timestep, const uint8_t *input, size_t input_size,
    rvrt_frame_t *workspace, uint32_t workspace_capacity);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_SESSION_IO_H */
