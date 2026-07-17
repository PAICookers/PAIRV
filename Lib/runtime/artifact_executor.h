#ifndef RVRT_ARTIFACT_EXECUTOR_H
#define RVRT_ARTIFACT_EXECUTOR_H

#include <stdint.h>

#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Caller-owned storage for one ExecutionPlan runtime buffer. */
typedef struct rvrt_executor_buffer_s {
    uint8_t *data;
    uint32_t capacity;
} rvrt_executor_buffer_t;

/** @brief Resources used by the generated artifact schedule interpreter. */
typedef struct rvrt_artifact_executor_config_s {
    rvrt_session_t *session;
    rvrt_executor_buffer_t *buffers;
    uint32_t buffer_count;
    rvrt_frame_t *workspace;
    uint32_t workspace_capacity;
    rvrt_voltage_decode_state_t *voltage_state;
    uint32_t voltage_state_capacity;
} rvrt_artifact_executor_config_t;

/** @brief Initialized ExecutionPlan interpreter state. */
typedef struct rvrt_artifact_executor_s {
    rvrt_session_t *session;
    rvrt_executor_buffer_t *buffers;
    uint32_t buffer_count;
    rvrt_frame_t *workspace;
    uint32_t workspace_capacity;
    rvrt_voltage_decode_state_t *voltage_state;
    uint32_t voltage_state_capacity;
    uint32_t input_ref;
    uint32_t input_bytes;
    uint32_t output_ref;
    uint32_t output_bytes;
} rvrt_artifact_executor_t;

/** @brief Validate and bind a complete generated ExecutionPlan. */
rvrt_session_status_t
rvrt_artifact_executor_init(rvrt_artifact_executor_t *executor,
                            const rvrt_artifact_executor_config_t *config);

/** @brief Execute one input through every ordered PAICORE/CPU stage. */
rvrt_session_status_t
rvrt_artifact_executor_run(rvrt_artifact_executor_t *executor,
                           const uint8_t *input, uint32_t input_size,
                           uint32_t timeout_ms);

/** @brief Borrow the final generated-plan output buffer. */
rvrt_session_status_t
rvrt_artifact_executor_get_output(const rvrt_artifact_executor_t *executor,
                                  const uint8_t **data, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_ARTIFACT_EXECUTOR_H */
