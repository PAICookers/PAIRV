#ifndef RVRT_PAICORE_RUNNER_H
#define RVRT_PAICORE_RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Output reception policy selected when a runner is prepared. */
typedef enum rvrt_paicore_runner_rx_policy_e {
    /** Preserve sparse output behavior: omitted DATA values decode as zero. */
    RVRT_PAICORE_RUNNER_RX_SPARSE = 0,
    /** Require one unique frame for every output value or VOLTAGE lane. */
    RVRT_PAICORE_RUNNER_RX_EXACT = 1,
} rvrt_paicore_runner_rx_policy_t;

/** @brief Lifecycle state of caller-owned runner storage. */
typedef enum rvrt_paicore_runner_state_e {
    RVRT_PAICORE_RUNNER_EMPTY = 0,
    RVRT_PAICORE_RUNNER_PREPARED = 1,
    RVRT_PAICORE_RUNNER_ATTACHED = 2,
} rvrt_paicore_runner_state_t;

/**
 * @brief Immutable resources parsed and validated without taking the IRQ.
 *
 * artifact_data is borrowed by a successful prepare and must remain valid and
 * byte-for-byte unchanged until rvrt_paicore_runner_release().
 */
typedef struct rvrt_paicore_runner_prepare_config_s {
    const uint8_t *artifact_data;
    size_t artifact_size;
    rvrt_voltage_decode_state_t *voltage_state;
    uint32_t voltage_state_capacity;
    uint32_t timeout_ms;
    rvrt_paicore_runner_rx_policy_t rx_policy;
} rvrt_paicore_runner_prepare_config_t;

/**
 * @brief Shared scratch resources borrowed while one runner is attached.
 *
 * The referenced scratch storage must remain valid until detach or release.
 */
typedef struct rvrt_paicore_runner_attach_config_s {
    rvrt_frame_t *frame_buffer;
    uint32_t frame_capacity;
    uint32_t *coverage_bitmap;
    uint32_t coverage_word_capacity;
} rvrt_paicore_runner_attach_config_t;

/** @brief Caller-owned resources used to deploy one PAICORE sample runner. */
typedef struct rvrt_paicore_runner_deploy_config_s {
    /** Verified artifact bytes; must remain valid while the runner is deployed.
     */
    const uint8_t *artifact_data;
    /** Capacity of artifact_data in bytes. */
    size_t artifact_size;
    /** Frame storage reused for input encoding and reset completion receive. */
    rvrt_frame_t *frame_buffer;
    /** Capacity of frame_buffer in rvrt_frame_t entries, not bytes. */
    uint32_t frame_capacity;
    /** Caller-owned VOLTAGE lane state, or NULL for DATA output. */
    rvrt_voltage_decode_state_t *voltage_state;
    /** Capacity in VOLTAGE state elements; must be T * output elements. */
    uint32_t voltage_state_capacity;
    /** Nonzero timeout applied to reset and sample completion. */
    uint32_t timeout_ms;
} rvrt_paicore_runner_deploy_config_t;

/**
 * @brief Deployed sample-level runner for the first PAICORE artifact I/O pair.
 *
 * Applications own this structure's storage and must zero-initialize it before
 * its first prepare or deploy. The runtime owns its member state from a
 * successful prepare or deploy through release; applications must not inspect
 * or modify members during that interval.
 *
 * A prepared runner borrows immutable artifact bytes until release. An attached
 * runner additionally borrows its frame and coverage scratch until detach or
 * release. The current NoC IRQ implementation permits only one attached runner
 * at a time; multiple prepared runners may coexist with disjoint caller-owned
 * storage.
 */
typedef struct rvrt_paicore_runner_s {
    rvrt_artifact_t artifact;
    rvrt_session_t session;
    rvrt_artifact_input_mapping_view_t input_view;
    rvrt_artifact_output_mapping_view_t output_view;
    rvrt_artifact_runtime_t runtime;
    rvrt_voltage_decode_state_t *voltage_state;
    uint32_t voltage_state_capacity;
    uint32_t timeout_ms;
    uint32_t encode_frame_capacity;
    size_t input_row_bytes;
    size_t output_row_bytes;
    uint32_t *coverage_bitmap;
    uint32_t coverage_word_capacity;
    uint32_t exact_slot_count;
    uint32_t expected_thread_id;
    uint32_t fast_output_base;
    rvrt_paicore_runner_rx_policy_t rx_policy;
    rvrt_paicore_runner_state_t state;
    bool config_loaded;
    bool config_load_failed;
    bool has_fast_data_layout;
    bool has_fast_voltage_layout;
} rvrt_paicore_runner_t;

/**
 * @brief Parse and validate an artifact without taking shared IRQ ownership.
 *
 * runner must be zero-initialized and EMPTY. On success it becomes PREPARED
 * and borrows config->artifact_data until release. If validation of an EMPTY
 * runner fails, it is cleared back to EMPTY and owns no caller resource.
 *
 * @return RVRT_SESSION_OK when the runner is prepared.
 * @return RVRT_SESSION_BUFFER_TOO_SMALL when VOLTAGE state is insufficient.
 * @return RVRT_SESSION_SCHEDULE_UNSUPPORTED when the input schedule does not
 *         fit the supported timestamp domain.
 * @return RVRT_SESSION_RUNTIME_ERROR for invalid resources, malformed or
 *         unsupported artifact metadata, or a non-EMPTY runner.
 */
rvrt_session_status_t
rvrt_paicore_runner_prepare(rvrt_paicore_runner_t *runner,
                            const rvrt_paicore_runner_prepare_config_t *config);

/**
 * @brief Attach a prepared runner to the shared IRQ/session scratch.
 *
 * runner must be PREPARED and no other runner may be attached. On success it
 * becomes ATTACHED and borrows the supplied frame buffer and, for EXACT RX,
 * coverage bitmap until detach or release.
 */
rvrt_session_status_t
rvrt_paicore_runner_attach(rvrt_paicore_runner_t *runner,
                           const rvrt_paicore_runner_attach_config_t *config);

/**
 * @brief Submit static configuration for an attached runner exactly once.
 *
 * A successful load is retained across detach/attach cycles. A failed load is
 * sticky: do not retry inference or configuration with this runner. Detach and
 * release it, then perform any required platform recovery before starting with
 * fresh zero-initialized storage.
 */
rvrt_session_status_t
rvrt_paicore_runner_load_config(rvrt_paicore_runner_t *runner);

/**
 * @brief Relinquish shared IRQ/scratch ownership and return to PREPARED.
 *
 * No barrier may be active. Artifact-derived state and a successful config
 * load remain cached for a later attach. This call does not unload PAICORE
 * configuration or recover hardware after a transport failure.
 */
rvrt_session_status_t rvrt_paicore_runner_detach(rvrt_paicore_runner_t *runner);

/**
 * @brief Caller-owned timing storage for one complete runner sample.
 *
 * `sync_round_trip_cycles` receives one measurement for every submitted SYNC
 * barrier. Each value spans SYNC submission through its COMPLETE reception;
 * it includes transport, PAICORE execution, IRQ service, and output handling.
 * It is not a PAICORE-core-only performance counter.
 */
typedef struct rvrt_paicore_runner_sample_timing_s {
    /** Reset INIT submission through INIT COMPLETE reception. */
    rv_counter_t init_round_trip_cycles;
    /** Storage for per-SYNC submission-to-COMPLETE round-trip cycles. */
    rv_counter_t *sync_round_trip_cycles;
    /** Number of entries available in sync_round_trip_cycles. */
    uint32_t sync_round_trip_capacity;
    /** Number of SYNC measurements written; may be partial after a failure. */
    uint32_t sync_round_trip_count;
} rvrt_paicore_runner_sample_timing_t;

/**
 * @brief Parse, attach, configure, and prepare the first PAICORE I/O pair.
 *
 * Before its first use, runner must be zero-initialized. Deploy first releases
 * any valid existing runner lifecycle state, then reads the artifact,
 * initializes its internal session on thread zero, loads static configuration
 * frames, and derives row sizes from mapping zero. Input encoding reuses
 * frame_buffer before the RX barrier becomes active.
 *
 * @return RVRT_SESSION_OK when the runner is ready.
 * @return RVRT_SESSION_BUFFER_TOO_SMALL when VOLTAGE state is insufficient.
 * @return RVRT_SESSION_SCHEDULE_UNSUPPORTED when an input timestep cannot fit
 *         the verified PAICORE input window or the sample timestamp domain.
 * @return RVRT_SESSION_RUNTIME_ERROR for invalid deployment resources,
 * unreadable artifact metadata, unsupported mapping semantics, or session
 * initialization/configuration failures.
 */
rvrt_session_status_t
rvrt_paicore_runner_deploy(rvrt_paicore_runner_t *runner,
                           const rvrt_paicore_runner_deploy_config_t *config);

/**
 * @brief Release IRQ/session ownership and clear a prepared or deployed runner.
 *
 * This ends the artifact and scratch borrowing intervals and returns runner to
 * EMPTY. It does not recover PAICORE or flush stale RX data after a failed
 * barrier.
 * @return RVRT_SESSION_OK after release; otherwise a session lifecycle error.
 */
rvrt_session_status_t
rvrt_paicore_runner_release(rvrt_paicore_runner_t *runner);

/**
 * @brief Copy cumulative transport statistics for a deployed runner.
 *
 * Statistics accumulate from deploy until release. With
 * RVRT_ENABLE_STATS=0, the call succeeds and returns an all-zero
 * snapshot whose enabled member is false.
 * @param runner Successfully deployed runner.
 * @param stats Receives the diagnostic snapshot; must not be NULL.
 * @return RVRT_SESSION_OK on success; RVRT_SESSION_RUNTIME_ERROR for a NULL
 *         or undeployed runner, or a NULL stats pointer.
 */
rvrt_session_status_t
rvrt_paicore_runner_get_stats(const rvrt_paicore_runner_t *runner,
                              rvrt_session_stats_t *stats);

/**
 * @brief Reset and run one complete PAICORE-layer sample.
 *
 * The runner submits one absolute input timestep before each cumulative
 * timeline target. Every SYNC is an independent barrier ending in COMPLETE;
 * its private IRQ RX frame handler scatters output frames directly into the
 * complete sample tensor.
 *
 * Input and output may be disjoint, or may use the same base address and row
 * stride for rowwise in-place replacement. Other overlapping layouts are
 * rejected because an early output could overwrite an unsent input row.
 *
 * Every sample starts with reset/INIT. This resets both PAICORE model state and
 * the session timeline, so every layer sample uses local timesteps 0..T-1.
 *
 * input_stride and output_stride are byte distances between rows. Pass zero
 * for the artifact-derived compact row layout. DATA callers normally provide
 * uint8_t output storage; VOLTAGE callers normally provide int32_t storage.
 * @param runner Successfully deployed runner.
 * @param input Complete input sample storage.
 * @param input_capacity Readable input capacity in bytes.
 * @param input_stride Byte distance between input timestep rows; zero selects
 *        the compact artifact-derived row size.
 * @param output Complete output sample storage with uint8_t DATA or int32_t
 *        VOLTAGE elements.
 * @param output_capacity Writable output capacity in bytes.
 * @param output_stride Byte distance between output timestep rows; zero selects
 *        the compact artifact-derived row size.
 */
rvrt_session_status_t
rvrt_paicore_runner_run_sample(rvrt_paicore_runner_t *runner,
                               const uint8_t *input, size_t input_capacity,
                               size_t input_stride, void *output,
                               size_t output_capacity, size_t output_stride);

/**
 * @brief Run one sample and record INIT plus per-SYNC round-trip timings.
 *
 * This has the same execution semantics as rvrt_paicore_runner_run_sample().
 * `timing` is caller-owned and may be NULL. When non-NULL, its SYNC storage
 * must hold every barrier required by this artifact sample; otherwise no model
 * reset or input frame is sent and RVRT_SESSION_BUFFER_TOO_SMALL is returned.
 * If a reset or SYNC barrier fails after timing starts, the fields contain the
 * measurements already written, including the failed barrier's elapsed time.
 * With RVRT_ENABLE_STATS=0, timing is ignored: its storage is neither
 * validated nor written.
 */
rvrt_session_status_t rvrt_paicore_runner_run_sample_profiled(
    rvrt_paicore_runner_t *runner, const uint8_t *input, size_t input_capacity,
    size_t input_stride, void *output, size_t output_capacity,
    size_t output_stride, rvrt_paicore_runner_sample_timing_t *timing);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_PAICORE_RUNNER_H */
