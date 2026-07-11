#ifndef RVRT_TASK_API_H
#define RVRT_TASK_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RVRT_TASK_ABI_VERSION 1U

/** @brief Status returned by a generated CPU task implementation. */
typedef enum rvrt_task_status_e {
    RVRT_TASK_STATUS_OK = 0,
    RVRT_TASK_STATUS_BAD_ARGUMENT = 1,
    RVRT_TASK_STATUS_BUFFER_TOO_SMALL = 2,
    RVRT_TASK_STATUS_OUT_OF_RANGE = 3,
} rvrt_task_status_t;

/** @brief Borrowed contiguous input and output buffers for one CPU task call.
 */
typedef struct rvrt_task_io_s {
    const uint8_t *input;
    uint32_t input_size;
    uint8_t *output;
    uint32_t output_size;
} rvrt_task_io_t;

/**
 * @brief Execute one generated CPU task selected by its target-local index.
 *
 * Implemented by the PAIBox-generated rvrt_tasks.c linked with the runtime.
 * @param cpu_task_index Local index recorded by the artifact execution stage.
 * @param io Borrowed input/output buffers sized to the task contract.
 */
rvrt_task_status_t rvrt_task_run(uint32_t cpu_task_index,
                                 const rvrt_task_io_t *io);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_TASK_API_H */
