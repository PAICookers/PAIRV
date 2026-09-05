#ifndef RVRT_PAICORE_RUNNER_H
#define RVRT_PAICORE_RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

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
 * Applications own this structure's storage; runtime owns its member state
 * from successful deploy through release. Applications must not inspect or
 * modify members during that interval. Artifact bytes and buffer pointers are
 * borrowed. The current NoC IRQ implementation permits one deployed runner at
 * a time.
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
    bool has_fast_data_layout;
    bool has_fast_voltage_layout;
} rvrt_paicore_runner_t;

/**
 * @brief Parse, configure, and prepare the first PAICORE I/O pair for samples.
 *
 * The runner reads the artifact, initializes its internal session on thread
 * zero, loads static configuration frames, and derives row sizes from mapping
 * zero. Input encoding reuses frame_buffer before the RX barrier becomes
 * active.
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
 * @brief Release IRQ/session ownership and clear a deployed runner.
 *
 * This does not recover PAICORE or flush stale RX data after a failed barrier.
 * @return RVRT_SESSION_OK after release; otherwise a session lifecycle error.
 */
rvrt_session_status_t
rvrt_paicore_runner_release(rvrt_paicore_runner_t *runner);

/**
 * @brief Copy cumulative transport statistics for a deployed runner.
 *
 * Statistics accumulate from deploy until release. With
 * RVRT_SESSION_ENABLE_STATS=0, the call succeeds and returns an all-zero
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

#ifdef __cplusplus
}
#endif

#endif /* RVRT_PAICORE_RUNNER_H */
