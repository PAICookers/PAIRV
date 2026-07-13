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
#define RVRT_SESSION_ENABLE_STATS 1
#endif

/** @brief Result of configuring or executing one ordered runtime session. */
typedef enum rvrt_session_status_e {
    RVRT_SESSION_STATUS_OK = 0,
    RVRT_SESSION_STATUS_TIMEOUT = 1,
    RVRT_SESSION_STATUS_BUFFER_TOO_SMALL = 2,
    RVRT_SESSION_STATUS_OVERFLOW = 3,
    RVRT_SESSION_STATUS_HARDWARE_ERROR = 4,
    RVRT_SESSION_STATUS_RUNTIME_ERROR = 5,
} rvrt_session_status_t;

/** @brief IRQ-owned state for the currently armed PAICORE phase. */
typedef struct rvrt_session_phase_s {
    volatile bool armed;
    volatile bool done;
    volatile bool overflow;
    volatile bool hardware_error;
    volatile uint32_t rx_count;
    volatile uint32_t output_work_count;
    volatile uint32_t complete_count;
} rvrt_session_phase_t;

/** @brief Per-sample counters accumulated when statistics are enabled. */
typedef struct rvrt_session_stats_s {
    uint32_t sent_input_frames;
    uint32_t rx_frames;
    uint32_t output_work_frames;
    uint32_t complete_frames;
    uint32_t decoded_writes;
    uint32_t sync_phases;
    bool overflow;
    bool hardware_error;
    rv_counter_t sync_wait_cycles;
    rv_counter_t total_cycles;
} rvrt_session_stats_t;

/** @brief Caller-owned physical storage for one logical runtime buffer. */
typedef struct rvrt_session_buffer_s {
    uint8_t *data;
    uint32_t capacity;
} rvrt_session_buffer_t;

/** @brief Caller-owned resources used to initialize one runtime session. */
typedef struct rvrt_session_config_s {
    const rvrt_artifact_t *artifact;
    uint32_t thread_index;
    rvrt_frame_t *rx_frames;
    uint32_t rx_capacity;
    rvrt_session_buffer_t *buffers;
    uint32_t buffer_count;
    /**
     * Optional per-output-element membrane accumulation state, required only
     * when at least one PAICORE phase's output mapping kind is
     * RVRT_OUTPUT_VOLTAGE. May be NULL when no phase decodes membrane frames.
     */
    rvrt_membrane_decode_state_t *membrane_state;
    uint32_t membrane_state_capacity;
} rvrt_session_config_t;

/**
 * @brief Initialized single-thread ordered-stage execution state.
 *
 * The artifact, frame storage, and runtime buffers are borrowed from config
 * and must remain valid until the session is no longer used.
 */
typedef struct rvrt_session_s {
    const rvrt_artifact_t *artifact;
    uint32_t thread_index;
    rvrt_frame_t *rx_frames;
    uint32_t rx_capacity;
    rvrt_session_buffer_t *buffers;
    uint32_t buffer_count;
    uint32_t input_ref;
    uint32_t input_bytes;
    uint32_t output_ref;
    uint32_t output_bytes;
    rvrt_membrane_decode_state_t *membrane_state;
    uint32_t membrane_state_capacity;
#if RVRT_SESSION_ENABLE_STATS
    rvrt_session_stats_t stats;
#endif
    rvrt_session_phase_t phase;
} rvrt_session_t;

/**
 * @brief Send all static configuration frames to PAICORE once.
 *
 * One EvalSoC NoC-backed runtime session is active at a time.
 * @param artifact Verified artifact containing configuration frames.
 */
rvrt_session_status_t rvrt_session_load_config(const rvrt_artifact_t *artifact);

/**
 * @brief Validate caller storage and bind an artifact thread to a session.
 * @param session Receives initialized runtime state.
 * @param config Caller-owned artifact, FIFO capture, and tensor-buffer storage.
 */
rvrt_session_status_t rvrt_session_init(rvrt_session_t *session,
                                        const rvrt_session_config_t *config);

/**
 * @brief Run one sample through every ordered PAICORE and CPU-task stage.
 *
 * timeout_ms applies independently to each PAICORE phase completion wait.
 * @param session Initialized session.
 * @param input Contiguous external-input bytes.
 * @param input_size Exact logical byte length required by the first stage.
 * @param workspace Temporary frame storage reused while encoding each phase.
 * @param workspace_capacity Number of frames that fit in workspace.
 * @param timeout_ms Per-phase completion timeout in milliseconds.
 */
rvrt_session_status_t
rvrt_session_run_sample(rvrt_session_t *session, const uint8_t *input,
                        uint32_t input_size, rvrt_frame_t *workspace,
                        uint32_t workspace_capacity, uint32_t timeout_ms);

/**
 * @brief Borrow the final logical output buffer produced by the last stage.
 * @param data Receives a pointer owned by the session caller's buffer array.
 * @param size Receives the logical output size in bytes.
 */
rvrt_session_status_t rvrt_session_get_output(const rvrt_session_t *session,
                                              const uint8_t **data,
                                              uint32_t *size);

/**
 * @brief Copy session statistics, or zeros when statistics are disabled.
 * @param session Initialized session whose counters are read.
 * @param stats Receives a value copy of the statistics.
 */
rvrt_session_status_t rvrt_session_get_stats(const rvrt_session_t *session,
                                             rvrt_session_stats_t *stats);

const char *rvrt_session_status_string(rvrt_session_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_SESSION_H */
