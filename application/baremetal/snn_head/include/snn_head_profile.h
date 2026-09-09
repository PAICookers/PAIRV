#ifndef SNN_HEAD_PROFILE_H
#define SNN_HEAD_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "snn_head.h"

#ifndef SNN_HEAD_TIMING
#define SNN_HEAD_TIMING 0
#endif

#if (SNN_HEAD_TIMING != 0) && (SNN_HEAD_TIMING != 1)
#error "SNN_HEAD_TIMING must be 0 or 1"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SNN_HEAD_PROFILE_CPU_PREPROCESS = 0,
    SNN_HEAD_PROFILE_ARTIFACT_VALIDATE,
    SNN_HEAD_PROFILE_DEPLOY,
    SNN_HEAD_PROFILE_SAMPLE,
    SNN_HEAD_PROFILE_RELEASE,
    SNN_HEAD_PROFILE_CPU_POSTPROCESS,
} snn_head_profile_phase_t;

/*
 * One layer's timing record. total_cycles contains the six top-level phase
 * fields plus residual_cycles. Config/input/INIT/SYNC/IRQ counters are nested
 * deploy/sample details and must not be added to those phases again.
 * sync_round_trip covers SYNC through COMPLETE, including PAICore, NoC, IRQ,
 * and result return; it is not a PAICore-core performance counter.
 */
typedef struct {
    uint64_t total_cycles;
    uint64_t cpu_preprocess_cycles;
    uint64_t artifact_validate_cycles;
    uint64_t deploy_cycles;
    uint64_t sample_cycles;
    uint64_t config_submit_cycles;
    uint64_t input_encode_cycles;
    uint64_t input_submit_cycles;
    uint64_t init_round_trip_cycles;
    uint64_t sync_round_trip_cycles;
    uint64_t release_cycles;
    uint64_t cpu_postprocess_cycles;
    uint64_t residual_cycles;
    uint64_t sync_wait_cycles;
    uint64_t rx_irq_service_cycles;
    uint64_t sync_round_trip_steps[SNN_HEAD_TIMESTEPS];
    uint32_t config_frames;
    uint32_t input_frames;
    uint32_t output_work_frames;
    uint32_t complete_frames;
    uint32_t rx_irq_count;
} snn_head_layer_profile_t;

typedef struct {
    snn_head_layer_profile_t layers[SNN_HEAD_LAYER_COUNT];
} snn_head_profile_t;

typedef struct {
    /* UART phases are outside the snn_head_run_chunk() timing interval. */
    uint64_t input_payload_rx_cycles;
    uint64_t input_decode_cycles;
    uint64_t result_encode_tx_cycles;
} snn_head_transport_timing_t;

/** Return true only after writing all bytes; false aborts the report. */
typedef bool (*snn_head_profile_write_fn)(void *ctx, const char *data,
                                          size_t length);

const snn_head_profile_t *snn_head_profile_get(void);

#if SNN_HEAD_TIMING
void snn_head_profile_reset(void);
void snn_head_profile_layer_begin(snn_head_layer_id_t layer);
void snn_head_profile_layer_end(snn_head_layer_id_t layer);
void snn_head_profile_phase_begin(snn_head_profile_phase_t phase);
void snn_head_profile_phase_end(snn_head_profile_phase_t phase);
void snn_head_profile_record_runner(
    const uint64_t *sync_round_trip_cycles, uint32_t sync_round_trip_count,
    uint64_t init_round_trip_cycles, uint64_t config_submit_cycles,
    uint64_t input_encode_cycles, uint64_t input_submit_cycles,
    uint64_t sync_wait_cycles, uint64_t rx_irq_service_cycles,
    uint32_t config_frames, uint32_t input_frames, uint32_t output_work_frames,
    uint32_t complete_frames, uint32_t rx_irq_count);
#define SNN_HEAD_PROFILE_LAYER_BEGIN(layer) snn_head_profile_layer_begin(layer)
#define SNN_HEAD_PROFILE_LAYER_END(layer) snn_head_profile_layer_end(layer)
#define SNN_HEAD_PROFILE_PHASE_BEGIN(phase) snn_head_profile_phase_begin(phase)
#define SNN_HEAD_PROFILE_PHASE_END(phase) snn_head_profile_phase_end(phase)
#define SNN_HEAD_PROFILE_RESET() snn_head_profile_reset()
#else
#define SNN_HEAD_PROFILE_LAYER_BEGIN(layer) ((void)(layer))
#define SNN_HEAD_PROFILE_LAYER_END(layer) ((void)(layer))
#define SNN_HEAD_PROFILE_PHASE_BEGIN(phase) ((void)(phase))
#define SNN_HEAD_PROFILE_PHASE_END(phase) ((void)(phase))
#define SNN_HEAD_PROFILE_RESET() ((void)0)
#endif

/** Write a complete timing block; return false after the first write failure.
 */
bool snn_head_profile_write_report(
    snn_head_profile_write_fn write, void *ctx, uint32_t hz,
    uint64_t chain_cycles, const snn_head_transport_timing_t *transport);

#ifdef __cplusplus
}
#endif

#endif /* SNN_HEAD_PROFILE_H */
