#ifndef RVRT_SESSION_H
#define RVRT_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "frame_codec.h"
#include "nuclei_sdk_soc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RVRT_SESSION_ENABLE_STATS
/** Compile-time switch for optional session transport statistics. */
#define RVRT_SESSION_ENABLE_STATS 0
#endif

#if (RVRT_SESSION_ENABLE_STATS != 0) && (RVRT_SESSION_ENABLE_STATS != 1)
#error "RVRT_SESSION_ENABLE_STATS must be 0 or 1"
#endif

/** @brief Result of a PAICORE transport or synchronization operation. */
typedef enum rvrt_session_status_e {
    /** Operation completed successfully. */
    RVRT_SESSION_OK = 0,
    /** Completion frame did not arrive in time. */
    RVRT_SESSION_TIMEOUT = 1,
    /** Caller storage cannot satisfy an operation. */
    RVRT_SESSION_BUFFER_TOO_SMALL = 2,
    /** RX frame buffer filled before completion. */
    RVRT_SESSION_OVERFLOW = 3,
    /** NoC/FIFO operation reported an error. */
    RVRT_SESSION_HARDWARE_ERROR = 4,
    /** Invalid state, argument, or codec setup. */
    RVRT_SESSION_RUNTIME_ERROR = 5,
} rvrt_session_status_t;

/**
 * @brief IRQ-owned state for the currently armed synchronization barrier.
 *
 * This is exposed because rvrt_session_t is caller-allocated, but applications
 * must not read it for synchronization or modify it. Use
 * rvrt_session_sync_wait() and rvrt_session_get_stats() instead.
 */
typedef struct rvrt_session_phase_s {
    /** True only while the IRQ handler may append frames for this barrier. */
    volatile bool armed;
    /** Set by the IRQ handler after completion, overflow, or hardware error. */
    volatile bool done;
    /** Set when rx_frames has no room for another received frame. */
    volatile bool overflow;
    /** Set when the IRQ handler cannot read the NoC FIFO. */
    volatile bool hardware_error;
    /** Number of raw frames stored in the caller-owned RX buffer. */
    volatile uint32_t rx_count;
    /** Number of stored PAICORE work frames. */
    volatile uint32_t output_work_count;
    /** Number of stored completion frames. */
    volatile uint32_t complete_count;
} rvrt_session_phase_t;

/** @brief Transport counters accumulated when statistics are enabled. */
typedef struct rvrt_session_stats_s {
    /** Frames submitted through send_frames or control barriers. */
    uint32_t sent_frames;
    /** Raw frames received across completed and failed barriers. */
    uint32_t rx_frames;
    /** Received work-frame count. */
    uint32_t output_work_frames;
    /** Received completion-frame count. */
    uint32_t complete_frames;
    /** Number of barriers that completed without an error. */
    uint32_t sync_phases;
    /** Sticky indication that any barrier overflowed. */
    bool overflow;
    /** Sticky indication that any barrier observed a hardware error. */
    bool hardware_error;
    /** Sum of cycle counts spent waiting in synchronization barriers. */
    rv_counter_t sync_wait_cycles;
} rvrt_session_stats_t;

/**
 * @brief Caller-owned resources used to initialize one base session.
 *
 * artifact and rx_frames are borrowed for the session lifetime. The RX buffer
 * is filled only while rvrt_session_sync_wait() is armed.
 */
typedef struct rvrt_session_config_s {
    /** Verified artifact that owns static config frames and thread metadata. */
    const rvrt_artifact_t *artifact;
    /** Artifact I/O thread selected for configuration and synchronization. */
    uint32_t thread_index;
    /** Caller-owned storage for raw frames received during one sync barrier. */
    rvrt_frame_t *rx_frames;
    /** Number of rvrt_frame_t entries in rx_frames; must be nonzero. */
    uint32_t rx_capacity;
} rvrt_session_config_t;

/**
 * @brief Single-thread PAICORE transport and synchronization state.
 *
 * The application owns storage for this structure but must treat it as opaque
 * after rvrt_session_init(). The current NoC IRQ implementation supports one
 * active session at a time; do not initialize or use a second session while a
 * first session may receive frames.
 */
typedef struct rvrt_session_s {
    /** Borrowed artifact selected at initialization. */
    const rvrt_artifact_t *artifact;
    /** Selected artifact I/O thread. */
    uint32_t thread_index;
    /** Borrowed caller RX buffer. */
    rvrt_frame_t *rx_frames;
    /** Number of entries available in rx_frames. */
    uint32_t rx_capacity;
#if RVRT_SESSION_ENABLE_STATS
    /** Internal counters returned by rvrt_session_get_stats(). */
    rvrt_session_stats_t stats;
#endif
    /** Internal IRQ phase state; applications must not modify it. */
    rvrt_session_phase_t phase;
} rvrt_session_t;

/**
 * @brief Bind one artifact thread and register the NoC IRQ handler.
 *
 * Does not send configuration or input frames. Call rvrt_session_load_config()
 * after a successful initialization and before the first inference. A later
 * initialization replaces the active IRQ session, so applications must keep
 * only one session active.
 * @param session Caller-allocated session storage to initialize.
 * @param config Borrowed artifact/thread/RX-buffer configuration.
 * @return RVRT_SESSION_OK on success; RVRT_SESSION_RUNTIME_ERROR for invalid
 *         configuration, unreadable thread metadata, or IRQ registration
 * failure.
 */
rvrt_session_status_t rvrt_session_init(rvrt_session_t *session,
                                        const rvrt_session_config_t *config);

/**
 * @brief Send every static configuration frame bound to the session artifact.
 *
 * Invoke once after initialization, and again only after resetting PAICORE or
 * changing the deployed artifact. The session must not be inside a sync wait.
 * @param session Initialized session with a verified artifact.
 * @return RVRT_SESSION_OK on success; RVRT_SESSION_RUNTIME_ERROR for an armed
 *         session, missing/invalid config frames, or a transport setup error.
 */
rvrt_session_status_t rvrt_session_load_config(rvrt_session_t *session);

/**
 * @brief Send pre-encoded logical NoC frames while no barrier is armed.
 *
 * This is the low-level transport primitive used by session I/O helpers. A
 * zero frame_count is valid and frames may then be NULL. The call only submits
 * frames; it does not wait for PAICORE output or validate model semantics.
 * @param session Initialized, unarmed session.
 * @param frames Logical high/low frames to submit, or NULL when frame_count is
 * zero.
 * @param frame_count Number of frames to submit.
 * @return RVRT_SESSION_OK on success; RVRT_SESSION_RUNTIME_ERROR for invalid
 *         arguments or an armed/uninitialized session.
 */
rvrt_session_status_t rvrt_session_send_frames(rvrt_session_t *session,
                                               const rvrt_frame_t *frames,
                                               uint32_t frame_count);

/**
 * @brief Reset the deployed PAICORE model and wait for its completion frame.
 *
 * Sends one Type-2 initialization control frame through a complete barrier.
 * The initialization-phase RX frames are consumed internally because a
 * conforming PAICORE response contains only the completion frame. This resets
 * the model state without reloading static configuration frames. Applications
 * should call this at the boundary between independent samples; streaming
 * applications should not call it between input timesteps or samples.
 * @param session Initialized, unarmed session with a nonempty RX buffer.
 * @param timeout_ms Maximum wall-clock wait expressed in milliseconds.
 * @return RVRT_SESSION_OK after the completion frame; RVRT_SESSION_TIMEOUT,
 *         RVRT_SESSION_OVERFLOW, or RVRT_SESSION_HARDWARE_ERROR for a failed
 *         barrier; RVRT_SESSION_RUNTIME_ERROR for invalid state or init frame.
 */
rvrt_session_status_t rvrt_session_reset_model(rvrt_session_t *session,
                                               uint32_t timeout_ms);

/**
 * @brief Execute one indivisible synchronization barrier.
 *
 * The function clears and arms RX state, sends one synchronization control
 * frame, waits for completion, closes the phase, and returns the raw sequence
 * including the completion frame. Use the selected thread's artifact
 * runtime.sync_steps as sync_steps; applications do not need to derive it from
 * model depth. The returned frames borrow the configured RX buffer and remain
 * valid until the next synchronization call or session storage is changed.
 * Timeout, overflow, and hardware errors leave the session unarmed.
 * @param session Initialized, unarmed session with a nonempty RX buffer.
 * @param sync_steps Artifact synchronization payload for this inference pass.
 * @param timeout_ms Maximum wall-clock wait expressed in milliseconds.
 * @param rx_frames Receives the borrowed RX frame sequence; must not be NULL.
 * @param rx_frame_count Receives the number of stored frames; must not be NULL.
 * @return RVRT_SESSION_OK after a completion frame; RVRT_SESSION_TIMEOUT,
 *         RVRT_SESSION_OVERFLOW, or RVRT_SESSION_HARDWARE_ERROR for a failed
 *         barrier; RVRT_SESSION_RUNTIME_ERROR for invalid state or sync frame.
 */
rvrt_session_status_t rvrt_session_sync_wait(rvrt_session_t *session,
                                             uint32_t sync_steps,
                                             uint32_t timeout_ms,
                                             const rvrt_frame_t **rx_frames,
                                             uint32_t *rx_frame_count);

/**
 * @brief Copy accumulated transport statistics, or zeros when disabled.
 * @param session Initialized session whose counters are queried.
 * @param stats Receives a snapshot; must not be NULL.
 * @return RVRT_SESSION_OK on success; RVRT_SESSION_RUNTIME_ERROR for NULL
 *         arguments. With RVRT_SESSION_ENABLE_STATS=0, stats is zeroed.
 */
rvrt_session_status_t rvrt_session_get_stats(const rvrt_session_t *session,
                                             rvrt_session_stats_t *stats);

/**
 * @brief Return a static diagnostic string for a session status.
 * @param status Status returned by this API family.
 * @return NUL-terminated static string; "unknown" for an unrecognized value.
 *         The caller must not free or modify it.
 */
const char *rvrt_session_status_string(rvrt_session_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_SESSION_H */
