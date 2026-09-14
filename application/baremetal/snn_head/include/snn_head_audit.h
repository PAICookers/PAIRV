#ifndef SNN_HEAD_AUDIT_H
#define SNN_HEAD_AUDIT_H

#include "paicore_runner.h"

typedef struct {
    rvrt_frame_t request;
    rvrt_frame_t response;
    uint32_t expected_first_frame;
    uint16_t payload_frames;
    uint16_t immutable_start;
} snn_head_audit_query_t;

typedef struct {
    uint64_t cycles;
    uint32_t windows;
    uint32_t compared_frames;
    uint32_t failed_layer;
    uint32_t failed_query;
    uint32_t failed_frame;
    uint32_t expected_high, expected_low;
    uint32_t actual_high, actual_low;
    bool passed;
} snn_head_audit_stats_t;

const snn_head_audit_stats_t *snn_head_audit_stats(void);
const char *snn_head_audit_identity(void);
void snn_head_audit_begin(void);
void snn_head_audit_end(bool passed);
bool snn_head_audit_layer(rvrt_paicore_runner_t *runner,
                          const rvrt_artifact_t *artifact, unsigned layer,
                          bool immutable_only);

#endif
