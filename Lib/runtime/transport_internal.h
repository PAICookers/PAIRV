#ifndef RVRT_RUNTIME_TRANSPORT_INTERNAL_H
#define RVRT_RUNTIME_TRANSPORT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "frame_codec.h"
#include "nuclei_sdk_soc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RVRT_ENABLE_STATS
/** Compile-time switch for optional runtime transport statistics and timings.
 */
#define RVRT_ENABLE_STATS 0
#endif

#if (RVRT_ENABLE_STATS != 0) && (RVRT_ENABLE_STATS != 1)
#error "RVRT_ENABLE_STATS must be 0 or 1"
#endif

/** @brief Result of a PAICORE transport or synchronization operation. */
typedef enum rvrt_runtime_status_e {
    /** Operation completed successfully. */
    RVRT_RUNTIME_OK = 0,
    /** Completion frame did not arrive in time. */
    RVRT_RUNTIME_TIMEOUT = 1,
    /** Caller storage cannot satisfy an operation. */
    RVRT_RUNTIME_BUFFER_TOO_SMALL = 2,
    /** RX frame buffer filled before completion. */
    RVRT_RUNTIME_OVERFLOW = 3,
    /** NoC/FIFO operation reported an error. */
    RVRT_RUNTIME_HARDWARE_ERROR = 4,
    /** Invalid state, argument, or codec setup. */
    RVRT_RUNTIME_RUNTIME_ERROR = 5,
    /** Raw payload and PAICORE-timeline barriers were mixed in one epoch. */
    RVRT_RUNTIME_SYNC_MODE_ERROR = 6,
    /** Full input sample exceeds its work-frame timestamp capacity. */
    RVRT_RUNTIME_SCHEDULE_UNSUPPORTED = 7,
    /** A previous barrier failed; hardware recovery and reinit are required. */
    RVRT_RUNTIME_FAULTED = 8,
    /** Another session owns the process-wide PAICORE IRQ receiver. */
    RVRT_RUNTIME_BUSY = 9,
} rvrt_runtime_status_t;

/** @brief Synchronization interpretation selected since the latest reset. */
typedef enum rvrt_transport_sync_mode_e {
    /** No synchronization interpretation has been selected in this epoch. */
    RVRT_RUNTIME_SYNC_MODE_UNSET = 0,
    /** Control payloads are interpreted only by the caller. */
    RVRT_RUNTIME_SYNC_MODE_RAW_PAYLOAD = 1,
    /** Control payloads are monotonic PAICORE timeline targets. */
    RVRT_RUNTIME_SYNC_MODE_TIMELINE = 2,
} rvrt_runtime_sync_mode_t;

/**
 * @brief Handle one non-COMPLETE frame while an RX barrier is active.
 *
 * The callback executes in PAICORE's IRQ handler. It must not block, allocate,
 * or invoke another session operation. A non-OK return drains the remaining
 * barrier but makes the synchronization call fail with that status.
 */
typedef rvrt_runtime_status_t (*rvrt_runtime_rx_frame_handler_t)(
    void *user_data, const rvrt_frame_t *frame);

/**
 * @brief IRQ-owned state for the currently active synchronization RX barrier.
 *
 * This is exposed because rvrt_transport_t is caller-allocated, but
 * applications must not read it for synchronization or modify it. Use the
 * session synchronization APIs and rvrt_transport_get_stats() instead.
 */
typedef struct rvrt_transport_rx_barrier_s {
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
#if RVRT_ENABLE_STATS
    /** Optional count of raw frames received, including IRQ-handled frames. */
    volatile uint32_t received_count;
    /** Optional count of received PAICORE work frames. */
    volatile uint32_t output_work_count;
    /** Optional count of received completion frames. */
    volatile uint32_t complete_count;
#endif
    /** Optional IRQ-only handler for non-COMPLETE synchronization frames. */
    rvrt_runtime_rx_frame_handler_t rx_frame_handler;
    /** Opaque user data passed to rx_frame_handler. */
    void *rx_frame_handler_user_data;
    /** First non-OK rx_frame_handler status for this barrier. */
    volatile rvrt_runtime_status_t rx_frame_handler_status;
} rvrt_runtime_rx_barrier_t;

/** @brief Session-lifetime transport counters collected when enabled. */
typedef struct rvrt_transport_stats_s {
    /** True when counters were compiled into this runtime build. */
    bool enabled;
    /** Static config, input, and control frames submitted to NoC. */
    uint32_t sent_frames;
    /** Static configuration frames submitted during deployment. */
    uint32_t config_frames;
    /** Input work frames submitted by
     * rvrt_transport_send_input_timestep(). */
    uint32_t input_frames;
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
    /** Cycles in config lookup and static frame submission. */
    rv_counter_t config_submit_cycles;
    /** Cycles spent encoding mapped input chunks. */
    rv_counter_t input_encode_cycles;
    /** Cycles spent submitting encoded input chunks to NoC. */
    rv_counter_t input_submit_cycles;
    /** Cycles spent waiting for the model-reset INIT completion barrier. */
    rv_counter_t init_wait_cycles;
    /** Total service cycles across PAICORE NoC IRQ entries. */
    rv_counter_t rx_irq_service_cycles;
    /** Number of PAICORE NoC IRQ entries that began during active barriers. */
    uint32_t rx_irq_count;
} rvrt_runtime_stats_t;

/**
 * @brief Caller-owned resources used to initialize one base session.
 *
 * artifact and rx_frames are borrowed for the session lifetime. The RX buffer
 * is filled only while a session synchronization RX barrier is active.
 */
typedef struct rvrt_transport_config_s {
    /** Verified artifact that owns static config frames and thread metadata. */
    const rvrt_artifact_view_t *artifact;
    /** Caller-owned storage for raw frames received during one sync barrier. */
    rvrt_frame_t *rx_frames;
    /** Number of rvrt_frame_t entries in rx_frames, not bytes; must be nonzero.
     */
    uint32_t rx_capacity;
} rvrt_transport_config_t;

/**
 * @brief Shared PAICORE transport and barrier state, with no thread timeline.
 *
 * The application owns this structure's storage; runtime owns its member state
 * from rvrt_transport_init() through rvrt_transport_deinit().
 * Applications must not inspect or modify members during that interval. The
 * current NoC IRQ implementation supports one active session at a time. A
 * second initialization returns RVRT_RUNTIME_BUSY until
 * rvrt_transport_deinit() detaches the owner. Reinitializing after a
 * barrier failure is valid only after the platform has cleared any stale
 * PAICORE/NoC RX frames.
 */
typedef struct rvrt_transport_s {
    /** Borrowed artifact selected at initialization. */
    const rvrt_artifact_view_t *artifact;
    /** Borrowed caller RX buffer. */
    rvrt_frame_t *rx_frames;
    /** Number of entries available in rx_frames. */
    uint32_t rx_capacity;
    /** Set after a barrier failure; transport operations are then rejected. */
    bool faulted;
#if RVRT_ENABLE_STATS
    /** Internal counters returned by rvrt_transport_get_stats(). */
    rvrt_runtime_stats_t stats;
#endif
    /** Internal IRQ RX barrier state; applications must not modify it. */
    rvrt_runtime_rx_barrier_t rx_barrier;
} rvrt_transport_t;

/**
 * @brief Bind an artifact and register the NoC IRQ handler.
 *
 * Does not send configuration or input frames. Call
 * rvrt_transport_load_config() after a successful initialization and
 * before the first inference. A later initialization is rejected while any
 * session owns the PAICORE IRQ.
 * @param session Caller-allocated session storage to initialize.
 * @param config Borrowed artifact/RX-buffer configuration.
 * @return RVRT_RUNTIME_OK on success; RVRT_RUNTIME_RUNTIME_ERROR for invalid
 *         configuration or IRQ registration
 * failure; RVRT_RUNTIME_BUSY when another session is active.
 */
rvrt_runtime_status_t
rvrt_transport_init(rvrt_transport_t *session,
                    const rvrt_transport_config_t *config);

/**
 * @brief Detach a session from the process-wide PAICORE IRQ receiver.
 *
 * This operation is idempotent for an inactive session. It does not flush NoC
 * RX state or recover PAICORE after a failed barrier; callers must complete
 * that platform recovery before initializing another session.
 * @return RVRT_RUNTIME_OK after detaching and clearing session storage;
 *         RVRT_RUNTIME_BUSY when another session is active or this session is
 *         inside a control barrier; RVRT_RUNTIME_RUNTIME_ERROR for NULL.
 */
rvrt_runtime_status_t rvrt_transport_deinit(rvrt_transport_t *session);

/**
 * @brief Send every static configuration frame bound to the session artifact.
 *
 * Invoke once after initialization, and again only after resetting PAICORE or
 * changing the deployed artifact. The session must not be inside a sync wait.
 * @param session Initialized session with a verified artifact.
 * @return RVRT_RUNTIME_OK on success; RVRT_RUNTIME_RUNTIME_ERROR for an active
 *         session, missing/invalid config frames, or a transport setup error.
 *         RVRT_RUNTIME_FAULTED when a previous barrier failed.
 */
rvrt_runtime_status_t rvrt_transport_load_config(rvrt_transport_t *session);

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
 * @return RVRT_RUNTIME_OK on success; RVRT_RUNTIME_RUNTIME_ERROR for invalid
 *         arguments, a completion barrier, or an uninitialized session.
 *         RVRT_RUNTIME_FAULTED when a previous barrier failed.
 */
rvrt_runtime_status_t rvrt_transport_send_frames(rvrt_transport_t *session,
                                                 const rvrt_frame_t *frames,
                                                 uint32_t frame_count);

/**
 * @brief Copy accumulated transport statistics and their availability.
 *
 * Statistics are observational only and never affect session behavior. With
 * RVRT_ENABLE_STATS=0, counter-update code and session-internal counter state
 * are omitted; the public result structure remains unchanged.
 * @param session Initialized session whose counters are queried.
 * @param stats Receives a snapshot; must not be NULL.
 * @return RVRT_RUNTIME_OK on success; RVRT_RUNTIME_RUNTIME_ERROR for an
 *         uninitialized session or NULL argument. With
 *         RVRT_ENABLE_STATS=0, counters are zero and stats.enabled is
 *         false.
 */
rvrt_runtime_status_t rvrt_transport_get_stats(const rvrt_transport_t *session,
                                               rvrt_runtime_stats_t *stats);

/**
 * @brief Return a static diagnostic string for a session status.
 * @param status Status returned by this API family.
 * @return NUL-terminated static string; "unknown" for an unrecognized value.
 *         The caller must not free or modify it.
 */
const char *rvrt_transport_status_string(rvrt_runtime_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_RUNTIME_TRANSPORT_INTERNAL_H */
