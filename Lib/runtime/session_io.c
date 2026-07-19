#include "session_io.h"

rvrt_session_status_t rvrt_session_send_input_timestep(
    rvrt_session_t *session, const rvrt_artifact_input_mapping_view_t *mapping,
    uint32_t timestep, const uint8_t *input, size_t input_size,
    rvrt_frame_t *workspace, uint32_t workspace_capacity)
{
    if ((session == NULL) || (mapping == NULL) || (input == NULL) ||
        (workspace == NULL) || (workspace_capacity == 0U) ||
        (workspace_capacity > RVRT_MAX_WORKSPACE_FRAMES)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    rvrt_input_cursor_t cursor = {0};
    rvrt_input_cursor_init(&cursor, timestep);

    while (1) {
        uint32_t frame_count = 0U;
        const rvrt_status_t codec_status = rvrt_encode_input_chunk(
            mapping, &cursor, input, input_size, workspace, workspace_capacity,
            &frame_count);
        if (((codec_status != RVRT_STATUS_DONE) &&
             (codec_status != RVRT_STATUS_BUFFER_FULL)) ||
            ((codec_status == RVRT_STATUS_BUFFER_FULL) &&
             (frame_count == 0U))) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }

        const rvrt_session_status_t send_status =
            rvrt_session_send_frames(session, workspace, frame_count);
        if (send_status != RVRT_SESSION_OK) {
            return send_status;
        }
        if (codec_status == RVRT_STATUS_DONE) {
            return RVRT_SESSION_OK;
        }
    }
}
