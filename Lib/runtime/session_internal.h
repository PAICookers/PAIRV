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

rvrt_session_status_t rvrt_session_reset_model_with_exact_rx_handler(
    rvrt_session_t *session, uint32_t rx_goal, uint32_t timeout_ms,
    rvrt_session_rx_exact_frame_handler_t rx_frame_handler, void *user_data);

rvrt_session_status_t rvrt_session_sync_wait_until_with_exact_rx_handler(
    rvrt_session_t *session, uint32_t completed_timesteps, uint32_t rx_goal,
    uint32_t timeout_ms,
    rvrt_session_rx_exact_frame_handler_t rx_frame_handler, void *user_data);

/**
 * @brief Send one diagnostic request and let an exact handler delimit reply.
 *
 * This does not alter the raw/timeline synchronization epoch. A failed or
 * timed-out exchange faults the session like every other control barrier.
 */
rvrt_session_status_t rvrt_session_exchange_exact(
    rvrt_session_t *session, const rvrt_frame_t *request, uint32_t rx_goal,
    uint32_t timeout_ms, rvrt_session_rx_exact_frame_handler_t rx_frame_handler,
    void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_SESSION_INTERNAL_H */
