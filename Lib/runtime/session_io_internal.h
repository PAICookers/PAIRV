#ifndef RVRT_SESSION_IO_INTERNAL_H
#define RVRT_SESSION_IO_INTERNAL_H

#include "session_io.h"

rvrt_session_status_t rvrt_session_send_canonical_input_timestep(
    rvrt_session_t *session, const rvrt_artifact_input_mapping_view_t *mapping,
    const rvrt_artifact_input_entry_t *cover_prototypes, uint32_t cover_count,
    uint32_t timestep, const uint8_t *input, size_t input_size,
    rvrt_frame_t *workspace, uint32_t workspace_capacity);

#endif /* RVRT_SESSION_IO_INTERNAL_H */
