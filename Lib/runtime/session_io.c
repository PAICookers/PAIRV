#include "session_io.h"

#include "debug.h"
#include "frame_codec_internal.h"
#include "session_io_internal.h"

static rvrt_session_status_t send_input_timestep(
    rvrt_session_t *session, const rvrt_artifact_input_mapping_view_t *mapping,
    const rvrt_artifact_input_entry_t *cover_prototypes, uint32_t cover_count,
    uint32_t timestep, const uint8_t *input, size_t input_size,
    rvrt_frame_t *workspace, uint32_t workspace_capacity)
{
    const bool use_canonical = cover_prototypes != NULL;

    if ((session == NULL) || (mapping == NULL) || (input == NULL) ||
        (workspace == NULL) || (workspace_capacity == 0U)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (session->faulted) {
        return RVRT_SESSION_FAULTED;
    }

    rvrt_input_cursor_t cursor = {0};
    rvrt_input_cursor_init(&cursor, timestep);

    while (1) {
        uint32_t frame_count = 0U;
#if RVRT_ENABLE_STATS
        const rv_counter_t encode_start = __get_rv_cycle();
#endif
        const rvrt_codec_status_t codec_status =
            use_canonical
                ? rvrt_encode_input_canonical_chunk(
                      cover_prototypes, cover_count, mapping->element_count,
                      mapping->bit_width, &cursor, input, input_size, workspace,
                      workspace_capacity, &frame_count)
                : rvrt_encode_input_chunk(mapping, &cursor, input, input_size,
                                          workspace, workspace_capacity,
                                          &frame_count);
#if RVRT_ENABLE_STATS
        session->stats.input_encode_cycles += __get_rv_cycle() - encode_start;
#endif
        if (((codec_status != RVRT_CODEC_STATUS_DONE) &&
             (codec_status != RVRT_CODEC_STATUS_BUFFER_FULL)) ||
            ((codec_status == RVRT_CODEC_STATUS_BUFFER_FULL) &&
             (frame_count == 0U))) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }

        RV_DEBUG_LOGI("runtime", "input chunk encoded frames=%u status=%u",
                      (unsigned)frame_count, (unsigned)codec_status);
#if RVRT_ENABLE_STATS
        const rv_counter_t submit_start = __get_rv_cycle();
#endif
        const rvrt_session_status_t send_status =
            rvrt_session_send_frames(session, workspace, frame_count);
#if RVRT_ENABLE_STATS
        session->stats.input_submit_cycles += __get_rv_cycle() - submit_start;
        if (send_status == RVRT_SESSION_OK) {
            session->stats.input_frames += frame_count;
        }
#endif
        if (send_status != RVRT_SESSION_OK) {
            return send_status;
        }
        if (codec_status == RVRT_CODEC_STATUS_DONE) {
            return RVRT_SESSION_OK;
        }
    }
}

rvrt_session_status_t rvrt_session_send_input_timestep(
    rvrt_session_t *session, const rvrt_artifact_input_mapping_view_t *mapping,
    uint32_t timestep, const uint8_t *input, size_t input_size,
    rvrt_frame_t *workspace, uint32_t workspace_capacity)
{
    return send_input_timestep(session, mapping, NULL, 0U, timestep, input,
                               input_size, workspace, workspace_capacity);
}

rvrt_session_status_t rvrt_session_send_canonical_input_timestep(
    rvrt_session_t *session, const rvrt_artifact_input_mapping_view_t *mapping,
    const rvrt_artifact_input_entry_t *cover_prototypes, uint32_t cover_count,
    uint32_t timestep, const uint8_t *input, size_t input_size,
    rvrt_frame_t *workspace, uint32_t workspace_capacity)
{
    if ((cover_prototypes == NULL) || (cover_count == 0U)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    return send_input_timestep(session, mapping, cover_prototypes, cover_count,
                               timestep, input, input_size, workspace,
                               workspace_capacity);
}
