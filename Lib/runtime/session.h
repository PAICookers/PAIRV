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
#define RVRT_SESSION_ENABLE_STATS 0
#endif

/** @brief Result of a PAICORE transport or synchronization operation. */
typedef enum rvrt_session_status_e {
    RVRT_SESSION_OK = 0,
    RVRT_SESSION_TIMEOUT = 1,
    RVRT_SESSION_BUFFER_TOO_SMALL = 2,
    RVRT_SESSION_OVERFLOW = 3,
    RVRT_SESSION_HARDWARE_ERROR = 4,
    RVRT_SESSION_RUNTIME_ERROR = 5,
} rvrt_session_status_t;

/** @brief IRQ-owned state for the currently armed synchronization barrier. */
typedef struct rvrt_session_phase_s {
    volatile bool armed;
    volatile bool done;
    volatile bool overflow;
    volatile bool hardware_error;
    volatile uint32_t rx_count;
    volatile uint32_t output_work_count;
    volatile uint32_t complete_count;
} rvrt_session_phase_t;

/** @brief Transport counters accumulated when statistics are enabled. */
typedef struct rvrt_session_stats_s {
    uint32_t sent_frames;
    uint32_t rx_frames;
    uint32_t output_work_frames;
    uint32_t complete_frames;
    uint32_t sync_phases;
    bool overflow;
    bool hardware_error;
    rv_counter_t sync_wait_cycles;
} rvrt_session_stats_t;

/** @brief Caller-owned resources used to initialize one base session. */
typedef struct rvrt_session_config_s {
    const rvrt_artifact_t *artifact;
    uint32_t thread_index;
    rvrt_frame_t *rx_frames;
    uint32_t rx_capacity;
} rvrt_session_config_t;

/** @brief Single-thread PAICORE transport and synchronization state. */
typedef struct rvrt_session_s {
    const rvrt_artifact_t *artifact;
    uint32_t thread_index;
    rvrt_frame_t *rx_frames;
    uint32_t rx_capacity;
#if RVRT_SESSION_ENABLE_STATS
    rvrt_session_stats_t stats;
#endif
    rvrt_session_phase_t phase;
} rvrt_session_t;

/** @brief Bind artifact/thread control metadata and register the NoC IRQ. */
rvrt_session_status_t rvrt_session_init(rvrt_session_t *session,
                                        const rvrt_session_config_t *config);

/** @brief Send every static configuration frame bound to the session. */
rvrt_session_status_t rvrt_session_load_config(rvrt_session_t *session);

/** @brief Send already encoded frames while no synchronization phase is armed.
 */
rvrt_session_status_t rvrt_session_send_frames(rvrt_session_t *session,
                                               const rvrt_frame_t *frames,
                                               uint32_t frame_count);

/**
 * @brief Execute one indivisible synchronization barrier.
 *
 * The function clears and arms RX state, sends an explicit sync frame, waits
 * for completion, closes the phase, and returns the raw sequence including
 * the completion frame. Timeout and overflow leave the session unarmed.
 */
rvrt_session_status_t rvrt_session_sync_wait(rvrt_session_t *session,
                                             uint32_t sync_steps,
                                             uint32_t timeout_ms,
                                             const rvrt_frame_t **rx_frames,
                                             uint32_t *rx_frame_count);

/** @brief Copy transport statistics, or zeros when statistics are disabled. */
rvrt_session_status_t rvrt_session_get_stats(const rvrt_session_t *session,
                                             rvrt_session_stats_t *stats);

const char *rvrt_session_status_string(rvrt_session_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_SESSION_H */
