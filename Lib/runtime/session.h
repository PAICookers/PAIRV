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
    /** Raw payload and PAICORE-timeline barriers were mixed in one epoch. */
    RVRT_SESSION_SYNC_MODE_ERROR = 6,
    /** Full input sample exceeds its work-frame timestamp capacity. */
    RVRT_SESSION_SCHEDULE_UNSUPPORTED = 7,
    /** A previous barrier failed; hardware recovery and reinit are required. */
    RVRT_SESSION_FAULTED = 8,
    /** Another session owns the process-wide PAICORE IRQ receiver. */
    RVRT_SESSION_BUSY = 9,
} rvrt_session_status_t;

/** @brief Synchronization interpretation selected since the latest reset. */
typedef enum rvrt_session_sync_mode_e {
    /** No synchronization interpretation has been selected in this epoch. */
    RVRT_SESSION_SYNC_MODE_UNSET = 0,
    /** Control payloads are interpreted only by the caller. */
    RVRT_SESSION_SYNC_MODE_RAW_PAYLOAD = 1,
    /** Control payloads are monotonic PAICORE timeline targets. */
    RVRT_SESSION_SYNC_MODE_TIMELINE = 2,
} rvrt_session_sync_mode_t;

/**
 * @brief Handle one non-COMPLETE frame while an RX barrier is active.
 *
 * The callback executes in PAICORE's IRQ handler. It must not block, allocate,
 * or invoke another session operation. A non-OK return drains the remaining
 * barrier but makes the synchronization call fail with that status.
 */
typedef rvrt_session_status_t (*rvrt_session_rx_frame_handler_t)(
    void *user_data, const rvrt_frame_t *frame);

/**
 * @brief IRQ-owned state for the currently active synchronization RX barrier.
 *
 * This is exposed because rvrt_session_t is caller-allocated, but applications
 * must not read it for synchronization or modify it. Use
 * the session synchronization APIs and rvrt_session_get_stats() instead.
 */
typedef struct rvrt_session_rx_barrier_s {
    /** True only while the IRQ handler may append frames for this barrier. */
    volatile bool active;
    /** Set by the IRQ handler after completion, overflow, or hardware error. */
    volatile bool completed;
    /** Set when rx_frames has no room for another received frame. */
    volatile bool overflow;
    /** Set when the IRQ handler cannot read the NoC FIFO. */
    volatile bool hardware_error;
    /** Number of raw frames stored in the caller-owned RX buffer. */
    volatile uint32_t rx_count;
    /** Optional count of raw frames received, including IRQ-handled frames. */
    volatile uint32_t received_count;
    /** Optional count of received PAICORE work frames. */
    volatile uint32_t output_work_count;
    /** Optional count of received completion frames. */
    volatile uint32_t complete_count;
    /** Optional IRQ-only handler for non-COMPLETE synchronization frames. */
    rvrt_session_rx_frame_handler_t rx_frame_handler;
    /** Opaque user data passed to rx_frame_handler. */
    void *rx_frame_handler_user_data;
    /** First non-OK rx_frame_handler status for this barrier. */
    volatile rvrt_session_status_t rx_frame_handler_status;
} rvrt_session_rx_barrier_t;

/** @brief Session-lifetime transport counters collected when enabled. */
typedef struct rvrt_session_stats_s {
    /** True when counters were compiled into this runtime build. */
    bool enabled;
    /** Static config, input, and control frames submitted to NoC. */
    uint32_t sent_frames;
    /** Raw frames received across reset, sync, and failed barriers. */
    uint32_t rx_frames;
    /** Received work-frame count. */
    uint32_t output_work_frames;
    /** Received completion-frame count. */
    uint32_t complete_frames;
    /** Successful raw-payload/timeline sync barriers; excludes model reset. */
    uint32_t sync_barriers;
    /** Sticky indication that any barrier overflowed. */
    bool overflow;
    /** Sticky indication that any barrier observed a hardware error. */
    bool hardware_error;
    /** Cycles spent in raw/timeline sync waits; excludes model reset. */
    rv_counter_t sync_wait_cycles;
} rvrt_session_stats_t;

/**
 * @brief Caller-owned resources used to initialize one base session.
 *
 * artifact and rx_frames are borrowed for the session lifetime. The RX buffer
 * is filled only while a session synchronization RX barrier is active.
 */
typedef struct rvrt_session_config_s {
    /** Verified artifact that owns static config frames and thread metadata. */
    const rvrt_artifact_t *artifact;
    /** Artifact I/O thread selected for configuration and synchronization. */
    uint32_t thread_index;
    /** Caller-owned storage for raw frames received during one sync barrier. */
    rvrt_frame_t *rx_frames;
    /** Number of rvrt_frame_t entries in rx_frames, not bytes; must be nonzero.
     */
    uint32_t rx_capacity;
} rvrt_session_config_t;

/**
 * @brief Single-thread PAICORE transport and synchronization state.
 *
 * The application owns this structure's storage; runtime owns its member state
 * from rvrt_session_init() through rvrt_session_deinit(). Applications must not
 * inspect or modify members during that interval. The current NoC IRQ
 * implementation supports one active session at a time. A second initialization
 * returns RVRT_SESSION_BUSY until rvrt_session_deinit() detaches the owner.
 * Reinitializing after a barrier failure is valid only after the platform has
 * cleared any stale PAICORE/NoC RX frames.
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
    /** Synchronization interpretation selected since the latest reset. */
    rvrt_session_sync_mode_t sync_mode;
    /** Last successful cumulative completed-timestep count in this reset epoch.
     */
    uint32_t completed_timesteps;
    /** Set after a barrier failure; transport operations are then rejected. */
    bool faulted;
    /** Internal counters returned by rvrt_session_get_stats(). */
    rvrt_session_stats_t stats;
    /** Internal IRQ RX barrier state; applications must not modify it. */
    rvrt_session_rx_barrier_t rx_barrier;
} rvrt_session_t;

/**
 * @brief Bind one artifact thread and register the NoC IRQ handler.
 *
 * Does not send configuration or input frames. Call rvrt_session_load_config()
 * after a successful initialization and before the first inference. A later
 * initialization is rejected while any session owns the PAICORE IRQ.
 * @param session Caller-allocated session storage to initialize.
 * @param config Borrowed artifact/thread/RX-buffer configuration.
 * @return RVRT_SESSION_OK on success; RVRT_SESSION_RUNTIME_ERROR for invalid
 *         configuration, unreadable thread metadata, or IRQ registration
 * failure; RVRT_SESSION_BUSY when another session is active.
 */
rvrt_session_status_t rvrt_session_init(rvrt_session_t *session,
                                        const rvrt_session_config_t *config);

/**
 * @brief Detach a session from the process-wide PAICORE IRQ receiver.
 *
 * This operation is idempotent for an inactive session. It does not flush NoC
 * RX state or recover PAICORE after a failed barrier; callers must complete
 * that platform recovery before initializing another session.
 * @return RVRT_SESSION_OK after detaching and clearing session storage;
 *         RVRT_SESSION_BUSY when another session is active or this session is
 *         inside a control barrier; RVRT_SESSION_RUNTIME_ERROR for NULL.
 */
rvrt_session_status_t rvrt_session_deinit(rvrt_session_t *session);

/**
 * @brief Send every static configuration frame bound to the session artifact.
 *
 * Invoke once after initialization, and again only after resetting PAICORE or
 * changing the deployed artifact. The session must not be inside a sync wait.
 * @param session Initialized session with a verified artifact.
 * @return RVRT_SESSION_OK on success; RVRT_SESSION_RUNTIME_ERROR for an active
 *         session, missing/invalid config frames, or a transport setup error.
 *         RVRT_SESSION_FAULTED when a previous barrier failed.
 */
rvrt_session_status_t rvrt_session_load_config(rvrt_session_t *session);

/**
 * @brief Send pre-encoded logical NoC frames outside a completion barrier.
 *
 * This is the low-level transport primitive used by session I/O helpers. A
 * zero frame_count is valid and frames may then be NULL. The call only submits
 * frames; it does not wait for PAICORE output or validate model semantics.
 * @param session Initialized session outside a completion barrier.
 * @param frames Logical high/low frames to submit, or NULL when frame_count is
 * zero.
 * @param frame_count Number of frames to submit.
 * @return RVRT_SESSION_OK on success; RVRT_SESSION_RUNTIME_ERROR for invalid
 *         arguments, a completion barrier, or an uninitialized session.
 *         RVRT_SESSION_FAULTED when a previous barrier failed.
 */
rvrt_session_status_t rvrt_session_send_frames(rvrt_session_t *session,
                                               const rvrt_frame_t *frames,
                                               uint32_t frame_count);

/**
 * @brief Reset the deployed PAICORE model and wait for its completion frame.
 *
 * Sends one Type-2 initialization control frame through a complete barrier.
 * INIT-response frames before completion are discarded internally and do not
 * occupy the caller's RX buffer. This resets the PAICORE model state and
 * hardware timestep state without reloading static configuration frames; a
 * successful response also resets the session's cumulative sync timeline.
 * Applications should call this at the boundary between independent samples;
 * streaming applications should not call it between input timesteps or
 * samples.
 * @param session Initialized session with an inactive RX barrier and nonempty
 *        RX buffer.
 * @param timeout_ms Maximum wall-clock wait expressed in milliseconds.
 * @return RVRT_SESSION_OK after the completion frame; RVRT_SESSION_TIMEOUT or
 *         RVRT_SESSION_HARDWARE_ERROR for a failed barrier;
 *         RVRT_SESSION_RUNTIME_ERROR for invalid state or init frame.
 * A failed barrier faults the session and requires hardware recovery followed
 * by session reinitialization.
 */
rvrt_session_status_t rvrt_session_reset_model(rvrt_session_t *session,
                                               uint32_t timeout_ms);

/**
 * @brief Send an exact PAICORE synchronization payload and wait for completion.
 *
 * The function clears and arms RX state, sends one synchronization control
 * frame, waits for completion, closes the RX barrier, and returns the raw
 * sequence including the completion frame. It intentionally does not interpret
 * the payload as time. It is for protocol diagnostics and other raw-payload
 * users. Its first use after reset locks this session epoch to raw-payload
 * mode.
 * @param session Initialized session with an inactive RX barrier and nonempty
 *        RX buffer.
 * @param sync_payload Exact 24-bit PAICORE control payload.
 * @param timeout_ms Maximum wall-clock wait expressed in milliseconds.
 * @param rx_frames Receives the borrowed RX frame sequence; must not be NULL.
 * @param rx_frame_count Receives the number of stored frames; must not be NULL.
 * @return RVRT_SESSION_OK after a completion frame; RVRT_SESSION_TIMEOUT,
 *         RVRT_SESSION_OVERFLOW, or RVRT_SESSION_HARDWARE_ERROR for a failed
 *         barrier; RVRT_SESSION_SYNC_MODE_ERROR when this reset epoch already
 *         selected timeline mode; RVRT_SESSION_RUNTIME_ERROR otherwise.
 * A failed barrier faults the session and requires hardware recovery followed
 * by session reinitialization.
 */
rvrt_session_status_t rvrt_session_sync_wait_payload(
    rvrt_session_t *session, uint32_t sync_payload, uint32_t timeout_ms,
    const rvrt_frame_t **rx_frames, uint32_t *rx_frame_count);

/**
 * @brief Synchronize until a monotonic PAICORE timeline target completes.
 *
 * completed_timesteps is cumulative from the most recent successful model
 * reset. PAICORE control frames instead carry an incremental execution count,
 * so this function sends completed_timesteps minus the previous successful
 * target (or the target itself for the first timeline barrier). Its meaning is
 * defined by the deployed artifact's PAICORE schedule, not by application-row
 * indexing. Its first use after reset locks this session epoch to timeline
 * mode; raw-payload barriers then require another reset.
 * @param session Initialized session with an inactive RX barrier and nonempty
 *        RX buffer.
 * @param completed_timesteps Artifact-defined cumulative completed-timestep
 *        count. The first
 * successful timeline barrier after reset may target zero; later targets must
 * be strictly greater than the previous successful target.
 * @param timeout_ms Maximum wall-clock wait expressed in milliseconds.
 * @param rx_frames Receives the borrowed RX frame sequence; must not be NULL.
 * @param rx_frame_count Receives the number of stored frames; must not be NULL.
 * @return RVRT_SESSION_OK after a completion frame;
 * RVRT_SESSION_SYNC_MODE_ERROR for mixed modes or a non-increasing target; or
 * another session error. A failed barrier faults the session.
 */
rvrt_session_status_t rvrt_session_sync_wait_until(
    rvrt_session_t *session, uint32_t completed_timesteps, uint32_t timeout_ms,
    const rvrt_frame_t **rx_frames, uint32_t *rx_frame_count);

/**
 * @brief Copy accumulated transport statistics and their availability.
 *
 * Statistics are observational only and never affect session behavior. With
 * RVRT_SESSION_ENABLE_STATS=0, counter-update code is omitted while the public
 * structure layout remains unchanged.
 * @param session Initialized session whose counters are queried.
 * @param stats Receives a snapshot; must not be NULL.
 * @return RVRT_SESSION_OK on success; RVRT_SESSION_RUNTIME_ERROR for an
 *         uninitialized session or NULL argument. With
 *         RVRT_SESSION_ENABLE_STATS=0, counters are zero and stats.enabled is
 *         false.
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
