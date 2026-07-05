#ifndef RVRT_SESSION_H
#define RVRT_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "frame_codec.h"
#include "nuclei_sdk_soc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RVRT_SESSION_RX_MARGIN
#define RVRT_SESSION_RX_MARGIN 1U
#endif

#ifndef RVRT_SESSION_ENABLE_STATS
#define RVRT_SESSION_ENABLE_STATS 1
#endif

typedef enum rvrt_session_status_e {
    RVRT_SESSION_STATUS_OK = 0,
    RVRT_SESSION_STATUS_TIMEOUT = 1,
    RVRT_SESSION_STATUS_BUFFER_TOO_SMALL = 2,
    RVRT_SESSION_STATUS_OVERFLOW = 3,
    RVRT_SESSION_STATUS_HARDWARE_ERROR = 4,
    RVRT_SESSION_STATUS_RUNTIME_ERROR = 5,
} rvrt_session_status_t;

typedef struct rvrt_session_phase_s {
    volatile bool armed;
    volatile bool done;
    volatile bool overflow;
    volatile bool hardware_error;
    volatile uint32_t rx_count;
    volatile uint32_t output_work_count;
    volatile uint32_t complete_count;
    volatile uint32_t expected_output_work_count;
} rvrt_session_phase_t;

typedef struct rvrt_session_stats_s {
    uint32_t sent_input_frames;
    uint32_t rx_frames;
    uint32_t output_work_frames;
    uint32_t complete_frames;
    uint32_t decoded_writes;
    uint32_t sync_phases;
    bool overflow;
    bool hardware_error;
    rv_counter_t init_wait_cycles;
    rv_counter_t sync_wait_cycles;
    rv_counter_t total_cycles;
} rvrt_session_stats_t;

typedef struct rvrt_session_config_s {
    const rvrt_artifact_t *artifact;
    uint32_t thread_index;
    uint32_t input_index;
    uint32_t output_index;
    rvrt_frame_t *rx_frames;
    uint32_t rx_capacity;
    uint8_t *output;
    uint32_t output_size;
} rvrt_session_config_t;

typedef struct rvrt_session_s {
    const rvrt_artifact_t *artifact;
    uint32_t thread_index;
    uint32_t input_index;
    uint32_t output_index;
    rvrt_frame_t *rx_frames;
    uint32_t rx_capacity;
    uint8_t *output;
    uint32_t output_size;
    uint32_t output_entry_count;
#if RVRT_SESSION_ENABLE_STATS
    rvrt_session_stats_t stats;
#endif
    rvrt_session_phase_t phase;
} rvrt_session_t;

/* One EvalSoC NoC-backed runtime session is active at a time. */
rvrt_session_status_t
rvrt_session_replay_config(const rvrt_artifact_t *artifact);

rvrt_session_status_t rvrt_session_init(rvrt_session_t *session,
                                        const rvrt_session_config_t *config);

/* Sends the init frame and waits for the init completion IRQ. */
rvrt_session_status_t rvrt_session_start(rvrt_session_t *session,
                                         uint32_t timeout_ms);

/* Runs one complete sample: encode input, send sync, receive and decode output.
 */
rvrt_session_status_t
rvrt_session_run_sample(rvrt_session_t *session, const uint8_t *input,
                        uint32_t input_size, rvrt_frame_t *workspace,
                        uint32_t workspace_capacity, uint32_t timeout_ms);

rvrt_session_status_t rvrt_session_get_stats(const rvrt_session_t *session,
                                             rvrt_session_stats_t *stats);

const char *rvrt_session_status_string(rvrt_session_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_SESSION_H */
