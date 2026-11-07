#ifndef RVRT_THREAD_RUNNER_H
#define RVRT_THREAD_RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Resources used to bind one artifact thread to a shared session. */
typedef struct rvrt_thread_runner_open_config_s {
    rvrt_runtime_session_t *session;
    uint32_t thread_index;
    uint32_t input_mapping_index;
    uint32_t output_mapping_index;
    rvrt_voltage_decode_state_t *voltage_state;
    uint32_t voltage_state_capacity;
    uint32_t timeout_ms;
} rvrt_thread_runner_open_config_t;

/** @brief Optional timing storage for one ThreadRunner sample. */
typedef struct rvrt_thread_runner_sample_timing_s {
    rv_counter_t init_round_trip_cycles;
    rv_counter_t *sync_round_trip_cycles;
    uint32_t sync_round_trip_capacity;
    uint32_t sync_round_trip_count;
} rvrt_thread_runner_sample_timing_t;

/**
 * @brief Caller-owned state for one artifact thread and mapping pair.
 *
 * The structure must be zero-initialized before its first open call.
 */
typedef struct rvrt_thread_runner_s {
    rvrt_runtime_session_t *session;
    const rvrt_artifact_view_t *artifact;
    rvrt_artifact_input_mapping_view_t input_view;
    rvrt_artifact_output_mapping_view_t output_view;
    rvrt_artifact_runtime_t runtime;
    rvrt_voltage_decode_state_t *voltage_state;
    uint32_t voltage_state_capacity;
    uint32_t thread_index;
    uint32_t input_mapping_index;
    uint32_t output_mapping_index;
    uint32_t timeout_ms;
    uint32_t encode_frame_capacity;
    size_t input_row_bytes;
    size_t output_row_bytes;
    bool has_fast_data_layout;
    bool has_fast_voltage_layout;
    rvrt_runtime_sync_mode_t sync_mode;
    uint32_t completed_timesteps;
    bool opened;
    bool active;
} rvrt_thread_runner_t;

/** @brief Open a runner without sending config, INIT, or SYNC frames. */
rvrt_runtime_status_t
rvrt_thread_runner_open(rvrt_thread_runner_t *runner,
                        const rvrt_thread_runner_open_config_t *config);

/** @brief Close a runner while retaining the shared session. */
rvrt_runtime_status_t rvrt_thread_runner_close(rvrt_thread_runner_t *runner);

/** @brief Run one complete sample with per-timestep completion barriers. */
rvrt_runtime_status_t
rvrt_thread_runner_run_sample(rvrt_thread_runner_t *runner,
                              const uint8_t *input, size_t input_capacity,
                              size_t input_stride, void *output,
                              size_t output_capacity, size_t output_stride);

/** @brief Run one sample and optionally record barrier timings. */
rvrt_runtime_status_t rvrt_thread_runner_run_sample_profiled(
    rvrt_thread_runner_t *runner, const uint8_t *input, size_t input_capacity,
    size_t input_stride, void *output, size_t output_capacity,
    size_t output_stride, rvrt_thread_runner_sample_timing_t *timing);

/** @brief Read statistics from the runner's shared session. */
rvrt_runtime_status_t
rvrt_thread_runner_get_stats(const rvrt_thread_runner_t *runner,
                             rvrt_runtime_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_THREAD_RUNNER_H */
