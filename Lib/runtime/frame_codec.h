#ifndef RVRT_FRAME_CODEC_H
#define RVRT_FRAME_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#include "artifact_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rvrt_status_e {
    RVRT_STATUS_OK = 0,
    RVRT_STATUS_DONE = 1,
    RVRT_STATUS_BUFFER_FULL = 2,
    RVRT_STATUS_NULL_ARGUMENT = -1,
    RVRT_STATUS_ARTIFACT_ERROR = -2,
    RVRT_STATUS_OUT_OF_RANGE = -3,
    RVRT_STATUS_BAD_VALUE = -4,
    RVRT_STATUS_UNSUPPORTED = -5,
} rvrt_status_t;

typedef struct rvrt_frame_s {
    uint32_t high;
    uint32_t low;
} rvrt_frame_t;

#define RVRT_FRAME_TYPE_OFFSET 30U
#define RVRT_FRAME_TYPE_WORK 2U
#define RVRT_FRAME_KIND_OFFSET 28U
#define RVRT_FRAME_KIND_COMPLETE 0xEU

static inline bool rvrt_frame_is_work(const rvrt_frame_t *frame)
{
    return (frame != NULL) && (((frame->high >> RVRT_FRAME_TYPE_OFFSET) &
                                0x3U) == RVRT_FRAME_TYPE_WORK);
}

static inline bool rvrt_frame_is_complete(const rvrt_frame_t *frame)
{
    return (frame != NULL) && (((frame->high >> RVRT_FRAME_KIND_OFFSET) &
                                0xFU) == RVRT_FRAME_KIND_COMPLETE);
}

typedef struct rvrt_input_cursor_s {
    uint32_t thread_index;
    uint32_t input_index;
    uint32_t entry_index;
    uint32_t timestep;
} rvrt_input_cursor_t;

rvrt_status_t rvrt_build_init_frame(const rvrt_artifact_t *artifact,
                                    uint32_t thread_index, rvrt_frame_t *frame);

rvrt_status_t rvrt_build_sync_frame(const rvrt_artifact_t *artifact,
                                    uint32_t thread_index, rvrt_frame_t *frame);

void rvrt_input_cursor_init(rvrt_input_cursor_t *cursor, uint32_t thread_index,
                            uint32_t input_index, uint32_t timestep);

rvrt_status_t rvrt_encode_input_chunk(const rvrt_artifact_t *artifact,
                                      rvrt_input_cursor_t *cursor,
                                      const uint8_t *input, uint32_t input_size,
                                      rvrt_frame_t *frames,
                                      uint32_t frame_capacity,
                                      uint32_t *frame_count);

rvrt_status_t
rvrt_decode_output_frame(const rvrt_artifact_t *artifact, uint32_t thread_index,
                         uint32_t output_index, const rvrt_frame_t *frame,
                         uint8_t *output, uint32_t output_size, bool *written);

const char *rvrt_status_string(rvrt_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_FRAME_CODEC_H */
