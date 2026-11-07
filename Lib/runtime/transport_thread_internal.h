#ifndef RVRT_RUNTIME_TRANSPORT_THREAD_INTERNAL_H
#define RVRT_RUNTIME_TRANSPORT_THREAD_INTERNAL_H

#include "transport_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Raw diagnostics keep their mode in caller-owned thread state, not transport.
 * Raw and timeline barriers cannot be mixed before a successful reset. */
rvrt_runtime_status_t rvrt_transport_sync_wait_payload_for_thread(
    rvrt_transport_t *session, uint32_t thread_index,
    rvrt_runtime_sync_mode_t *sync_mode, uint32_t sync_payload,
    uint32_t timeout_ms, const rvrt_frame_t **rx_frames,
    uint32_t *rx_frame_count);

/** @brief Reset a specific artifact thread using the shared transport owner. */
rvrt_runtime_status_t rvrt_transport_reset_model_for_thread(
    rvrt_transport_t *session, uint32_t thread_index, uint32_t timeout_ms,
    rvrt_runtime_sync_mode_t *sync_mode, uint32_t *completed_timesteps);

/**
 * @brief Run a timeline barrier for a specific thread with caller-owned state.
 *
 * This keeps per-thread timeline progress outside the shared session while
 * reusing its single IRQ/RX barrier and transport fault handling.
 * Supply both raw-result pointers or a handler. The handler must not block,
 * allocate, perform transport operations, or retain the frame pointer.
 */
rvrt_runtime_status_t rvrt_transport_sync_wait_until_for_thread(
    rvrt_transport_t *session, uint32_t thread_index,
    uint32_t completed_timesteps, uint32_t timeout_ms,
    rvrt_runtime_sync_mode_t *sync_mode, uint32_t *previous_completed_timesteps,
    const rvrt_frame_t **rx_frames, uint32_t *rx_frame_count,
    rvrt_runtime_rx_frame_handler_t rx_frame_handler, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_RUNTIME_TRANSPORT_THREAD_INTERNAL_H */
