#ifndef RVRT_SESSION_INTERNAL_H
#define RVRT_SESSION_INTERNAL_H

#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reach a timeline target and handle non-COMPLETE frames in IRQ.
 *
 * Internal runner/probe support only. The handler must not block, allocate,
 * perform session operations, or retain the frame pointer.
 */
rvrt_session_status_t rvrt_session_sync_wait_until_with_rx_handler(
    rvrt_session_t *session, uint32_t completed_timesteps, uint32_t timeout_ms,
    rvrt_session_rx_frame_handler_t rx_frame_handler, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_SESSION_INTERNAL_H */
