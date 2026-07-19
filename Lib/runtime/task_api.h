#ifndef RVRT_TASK_API_H
#define RVRT_TASK_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Generated CPU-task ABI version. */
#define RVRT_TASK_ABI_VERSION 1U

/** @brief Status returned by a generated CPU task implementation. */
typedef enum rvrt_task_status_e {
    /** Task completed successfully. */
    RVRT_TASK_STATUS_OK = 0,
    /** Input/output descriptor is invalid. */
    RVRT_TASK_STATUS_BAD_ARGUMENT = 1,
    /** Buffer cannot hold task data. */
    RVRT_TASK_STATUS_BUFFER_TOO_SMALL = 2,
    /** Task index or value is unsupported. */
    RVRT_TASK_STATUS_OUT_OF_RANGE = 3,
} rvrt_task_status_t;

/**
 * @brief Borrowed contiguous input and output buffers for one CPU task call.
 *
 * The generated task owns neither buffer. Its generated contract determines
 * the required byte counts, layout, dtype, and whether in-place operation is
 * valid.
 */
typedef struct rvrt_task_io_s {
    /** Input bytes owned by the runtime buffer selected by the artifact. */
    const uint8_t *input;
    /** Number of readable bytes in input. */
    uint32_t input_size;
    /** Output bytes owned by the runtime buffer selected by the artifact. */
    uint8_t *output;
    /** Number of writable bytes in output. */
    uint32_t output_size;
} rvrt_task_io_t;

/**
 * @brief Execute one generated CPU task selected by its target-local index.
 *
 * Implemented by the PAIBox-generated rvrt_tasks.c linked with the runtime.
 * Applications normally reach this ABI through a schedule executor rather
 * than calling it directly.
 * @param cpu_task_index Target-local index recorded by the artifact stage.
 * @param io Borrowed input/output buffers sized and laid out for that task.
 * @return RVRT_TASK_STATUS_OK on success; otherwise a generated-task contract
 *         failure such as bad arguments, insufficient buffers, or an invalid
 *         task index/value.
 */
rvrt_task_status_t rvrt_task_run(uint32_t cpu_task_index,
                                 const rvrt_task_io_t *io);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_TASK_API_H */
