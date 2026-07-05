#ifndef RVRT_ARTIFACT_READER_H
#define RVRT_ARTIFACT_READER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RVRT_ARTIFACT_ALIGNMENT 8U

typedef enum rvrt_artifact_status_e {
    RVRT_ARTIFACT_OK = 0,
    RVRT_ARTIFACT_NULL_ARGUMENT = 1,
    RVRT_ARTIFACT_VERIFY_FAILED = 2,
    RVRT_ARTIFACT_MISSING_FIELD = 3,
    RVRT_ARTIFACT_OUT_OF_RANGE = 4,
    RVRT_ARTIFACT_BAD_ALIGNMENT = 5,
} rvrt_artifact_status_t;

typedef struct rvrt_artifact_s {
    const uint8_t *data;
    size_t size;
    const void *root;
} rvrt_artifact_t;

typedef struct rvrt_artifact_info_s {
    uint32_t schema_version;
    uint32_t config_word_count;
    uint32_t config_word_order;
    uint32_t thread_count;
} rvrt_artifact_info_t;

typedef struct rvrt_artifact_capacity_s {
    uint32_t input_bytes;
    uint32_t output_bytes;
    uint32_t output_entry_count;
    uint32_t rx_frame_count;
    uint32_t workspace_frame_count;
} rvrt_artifact_capacity_t;

typedef struct rvrt_artifact_core_offset_s {
    int32_t xy;
    int32_t x;
    int32_t y;
} rvrt_artifact_core_offset_t;

typedef struct rvrt_artifact_copy_count_s {
    int32_t xy;
    int32_t x;
    int32_t y;
} rvrt_artifact_copy_count_t;

typedef struct rvrt_artifact_runtime_s {
    uint32_t timesteps;
    uint32_t tick_depth;
    uint32_t sync_steps;
    uint32_t decode_mode;
} rvrt_artifact_runtime_t;

typedef struct rvrt_artifact_input_entry_s {
    uint32_t elem_idx;
    rvrt_artifact_core_offset_t core_offset;
    rvrt_artifact_copy_count_t copy_count;
    uint32_t tick_relative;
    uint32_t addr_axon;
    uint32_t target_lcn;
    uint32_t copy_id;
    uint32_t dtype;
} rvrt_artifact_input_entry_t;

typedef struct rvrt_artifact_output_entry_s {
    uint32_t elem_idx;
    uint32_t copy_id;
    uint32_t axon_bit_idx;
    uint32_t dtype;
} rvrt_artifact_output_entry_t;

rvrt_artifact_status_t rvrt_artifact_read(const uint8_t *data, size_t size,
                                          rvrt_artifact_t *artifact);

rvrt_artifact_status_t rvrt_artifact_get_info(const rvrt_artifact_t *artifact,
                                              rvrt_artifact_info_t *info);

rvrt_artifact_status_t
rvrt_artifact_get_capacity(const rvrt_artifact_t *artifact,
                           uint32_t thread_index, uint32_t input_index,
                           uint32_t output_index,
                           rvrt_artifact_capacity_t *capacity);

rvrt_artifact_status_t
rvrt_artifact_config_word_count(const rvrt_artifact_t *artifact,
                                uint32_t *count);

rvrt_artifact_status_t
rvrt_artifact_config_word(const rvrt_artifact_t *artifact, uint32_t word_index,
                          uint32_t *word);

rvrt_artifact_status_t
rvrt_artifact_thread_count(const rvrt_artifact_t *artifact, uint32_t *count);

rvrt_artifact_status_t rvrt_artifact_thread_root_core_offset(
    const rvrt_artifact_t *artifact, uint32_t thread_index,
    rvrt_artifact_core_offset_t *root_core_offset);

rvrt_artifact_status_t
rvrt_artifact_thread_runtime(const rvrt_artifact_t *artifact,
                             uint32_t thread_index,
                             rvrt_artifact_runtime_t *runtime);

rvrt_artifact_status_t
rvrt_artifact_input_bit_width(const rvrt_artifact_t *artifact,
                              uint32_t thread_index, uint32_t input_index,
                              uint32_t *bit_width);

rvrt_artifact_status_t
rvrt_artifact_output_bit_width(const rvrt_artifact_t *artifact,
                               uint32_t thread_index, uint32_t output_index,
                               uint32_t *bit_width);

rvrt_artifact_status_t
rvrt_artifact_output_kind(const rvrt_artifact_t *artifact,
                          uint32_t thread_index, uint32_t output_index,
                          uint32_t *kind);

rvrt_artifact_status_t
rvrt_artifact_input_entry_count(const rvrt_artifact_t *artifact,
                                uint32_t thread_index, uint32_t input_index,
                                uint32_t *count);

rvrt_artifact_status_t
rvrt_artifact_output_entry_count(const rvrt_artifact_t *artifact,
                                 uint32_t thread_index, uint32_t output_index,
                                 uint32_t *count);

rvrt_artifact_status_t
rvrt_artifact_input_entry(const rvrt_artifact_t *artifact,
                          uint32_t thread_index, uint32_t input_index,
                          uint32_t entry_index,
                          rvrt_artifact_input_entry_t *entry);

rvrt_artifact_status_t
rvrt_artifact_output_entry(const rvrt_artifact_t *artifact,
                           uint32_t thread_index, uint32_t output_index,
                           uint32_t entry_index,
                           rvrt_artifact_output_entry_t *entry);

rvrt_artifact_status_t
rvrt_artifact_output_target_lcn(const rvrt_artifact_t *artifact,
                                uint32_t thread_index, uint32_t *target_lcn);

const char *rvrt_artifact_status_string(rvrt_artifact_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_ARTIFACT_READER_H */
