#ifndef RVRT_ARTIFACT_READER_H
#define RVRT_ARTIFACT_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RVRT_ARTIFACT_ALIGNMENT 8U
#define RVRT_ARTIFACT_MAX_RANK 8U
#define RVRT_STAGE_PAICORE 0U
#define RVRT_STAGE_CPU_TASK 1U

/** @brief Result of validating or reading an exported runtime artifact. */
typedef enum rvrt_artifact_status_e {
    RVRT_ARTIFACT_OK = 0,
    RVRT_ARTIFACT_NULL_ARGUMENT = 1,
    RVRT_ARTIFACT_VERIFY_FAILED = 2,
    RVRT_ARTIFACT_MISSING_FIELD = 3,
    RVRT_ARTIFACT_OUT_OF_RANGE = 4,
    RVRT_ARTIFACT_BAD_ALIGNMENT = 5,
    RVRT_ARTIFACT_BAD_VALUE = 6,
} rvrt_artifact_status_t;

/**
 * @brief Verified, non-owning handles into one FlatBuffer artifact.
 *
 * The backing byte buffer passed to rvrt_artifact_read() must remain aligned
 * and alive for the lifetime of this object and every view derived from it.
 */
typedef struct rvrt_artifact_s {
    const void *root;
    const void *io_mapping;
    const void *config_frames;
} rvrt_artifact_t;

/** @brief Small, model-independent artifact metadata. */
typedef struct rvrt_artifact_info_s {
    uint32_t schema_version;
    uint32_t config_word_count;
    uint32_t config_word_order;
    uint32_t thread_count;
} rvrt_artifact_info_t;

/**
 * @brief Logical buffer and frame capacities needed by one selected thread.
 *
 * Tensor byte counts are logical sizes. Callers may provide larger physical
 * buffers, but not smaller ones.
 */
typedef struct rvrt_artifact_capacity_s {
    uint32_t input_bytes;
    uint32_t final_output_bytes;
    uint32_t rx_frame_count;
    uint32_t workspace_frame_count;
    uint32_t final_output_dtype;
    uint32_t final_output_rank;
    int32_t final_output_shape[RVRT_ARTIFACT_MAX_RANK];
} rvrt_artifact_capacity_t;

/** @brief Target package identity and generated-task ABI requirement. */
typedef struct rvrt_artifact_runtime_target_s {
    const char *target_id;
    const char *profile_id;
    uint32_t required_task_abi_version;
} rvrt_artifact_runtime_target_t;

/** @brief Relative NoC destination coordinates from a thread root core. */
typedef struct rvrt_artifact_core_offset_s {
    int32_t xy;
    int32_t x;
    int32_t y;
} rvrt_artifact_core_offset_t;

/** @brief NoC multicast copy counts for the three routing dimensions. */
typedef struct rvrt_artifact_copy_count_s {
    int32_t xy;
    int32_t x;
    int32_t y;
} rvrt_artifact_copy_count_t;

/** @brief Static runtime timing metadata for one artifact thread. */
typedef struct rvrt_artifact_runtime_s {
    uint32_t timesteps;
    uint32_t tick_depth;
    uint32_t sync_steps;
    uint32_t decode_mode;
} rvrt_artifact_runtime_t;

/** @brief One logical input element encoded as an offline work frame. */
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

/** @brief One output-frame address mapped to a logical tensor element. */
typedef struct rvrt_artifact_output_entry_s {
    uint32_t elem_idx;
    uint32_t copy_id;
    uint32_t axon_bit_idx;
    uint32_t dtype;
} rvrt_artifact_output_entry_t;

/**
 * @brief Borrowed input-entry vector for streaming input encoding.
 *
 * Views borrow FlatBuffers storage owned by the artifact backing bytes.
 */
typedef struct rvrt_artifact_input_mapping_view_s {
    const void *entries;
    uint32_t entry_count;
    uint32_t bit_width;
} rvrt_artifact_input_mapping_view_t;

/** @brief Borrowed, key-sorted output-entry vector for frame decoding. */
typedef struct rvrt_artifact_output_mapping_view_s {
    const void *entries;
    uint32_t entry_count;
    uint32_t bit_width;
    uint32_t kind;
    uint32_t target_lcn;
} rvrt_artifact_output_mapping_view_t;

/** @brief Generated CPU task input and output runtime-buffer references. */
typedef struct rvrt_artifact_cpu_task_s {
    uint32_t input_ref;
    uint32_t output_ref;
} rvrt_artifact_cpu_task_t;

/** @brief One ordered execution-plan stage and its local table reference. */
typedef struct rvrt_artifact_stage_s {
    uint32_t stage_index;
    uint32_t kind;
    uint32_t ref_index;
} rvrt_artifact_stage_t;

/**
 * @brief PAICORE stage metadata used to encode, synchronize, and decode a pass.
 *
 * latency_ticks is a positive 24-bit control-frame payload computed by PAIBox.
 */
typedef struct rvrt_artifact_paicore_phase_s {
    uint32_t input_ref;
    uint32_t output_ref;
    uint32_t input_mapping_ref;
    uint32_t output_mapping_ref;
    uint32_t latency_ticks;
} rvrt_artifact_paicore_phase_t;

/**
 * @brief Verify an aligned FlatBuffer artifact and bind non-owning handles.
 * @param data Aligned artifact bytes that remain alive after this call.
 * @param size Byte length of data; must be nonzero.
 * @param artifact Receives verified root-table handles.
 * @return Artifact validation status.
 */
rvrt_artifact_status_t rvrt_artifact_read(const uint8_t *data, size_t size,
                                          rvrt_artifact_t *artifact);

/** @brief Read basic metadata without materializing runtime buffers. */
rvrt_artifact_status_t rvrt_artifact_get_info(const rvrt_artifact_t *artifact,
                                              rvrt_artifact_info_t *info);

/**
 * @brief Derive buffer and frame capacities for one artifact thread.
 * @param artifact Verified artifact backing every returned value.
 * @param thread_index Selected artifact thread.
 * @param capacity Receives logical tensor sizes and required frame counts.
 */
rvrt_artifact_status_t
rvrt_artifact_get_capacity(const rvrt_artifact_t *artifact,
                           uint32_t thread_index,
                           rvrt_artifact_capacity_t *capacity);

/** @brief Read the target package identity and required generated-task ABI. */
rvrt_artifact_status_t
rvrt_artifact_runtime_target(const rvrt_artifact_t *artifact,
                             rvrt_artifact_runtime_target_t *target);

rvrt_artifact_status_t
rvrt_artifact_cpu_task_count(const rvrt_artifact_t *artifact, uint32_t *count);

rvrt_artifact_status_t rvrt_artifact_cpu_task(const rvrt_artifact_t *artifact,
                                              uint32_t task_index,
                                              rvrt_artifact_cpu_task_t *task);

rvrt_artifact_status_t
rvrt_artifact_stage_count(const rvrt_artifact_t *artifact, uint32_t *count);

rvrt_artifact_status_t rvrt_artifact_stage(const rvrt_artifact_t *artifact,
                                           uint32_t stage_index,
                                           rvrt_artifact_stage_t *stage);

/**
 * @brief Resolve a stage's input and output runtime-buffer references.
 * @param stage Stage obtained from rvrt_artifact_stage().
 * @param input_ref Receives the stage input buffer index.
 * @param output_ref Receives the stage output buffer index.
 */
rvrt_artifact_status_t
rvrt_artifact_stage_buffer_refs(const rvrt_artifact_t *artifact,
                                const rvrt_artifact_stage_t *stage,
                                uint32_t *input_ref, uint32_t *output_ref);

rvrt_artifact_status_t
rvrt_artifact_paicore_phase_count(const rvrt_artifact_t *artifact,
                                  uint32_t *count);

/**
 * @brief Read one PAICORE phase, including its validated sync latency.
 * @param phase_index Local index in ExecutionPlan.paicore_phases.
 * @param phase Receives the phase buffer, mapping, and latency metadata.
 */
rvrt_artifact_status_t
rvrt_artifact_paicore_phase(const rvrt_artifact_t *artifact,
                            uint32_t phase_index,
                            rvrt_artifact_paicore_phase_t *phase);

rvrt_artifact_status_t
rvrt_artifact_runtime_buffer_count(const rvrt_artifact_t *artifact,
                                   uint32_t *count);

/**
 * @brief Derive a runtime buffer's logical byte size and tensor metadata.
 *
 * Any output pointer except artifact may be NULL when that field is not needed.
 * @param buffer_index Local index in ExecutionPlan.buffers.
 * @param bytes Optional logical byte-count output.
 * @param dtype Optional DataType numeric-code output.
 * @param rank Optional shape-rank output.
 * @param shape Optional RVRT_ARTIFACT_MAX_RANK dimension output.
 */
rvrt_artifact_status_t rvrt_artifact_runtime_buffer_bytes(
    const rvrt_artifact_t *artifact, uint32_t buffer_index, uint32_t *bytes,
    uint32_t *dtype, uint32_t *rank, int32_t *shape);

rvrt_artifact_status_t
rvrt_artifact_config_word_count(const rvrt_artifact_t *artifact,
                                uint32_t *count);

/**
 * @brief Read one logical configuration frame in high/low word order.
 * @param frame_index Zero-based frame index, not a raw word index.
 * @param high Receives logical bits [63:32].
 * @param low Receives logical bits [31:0].
 */
rvrt_artifact_status_t
rvrt_artifact_config_frame_words(const rvrt_artifact_t *artifact,
                                 uint32_t frame_index, uint32_t *high,
                                 uint32_t *low);

rvrt_artifact_status_t
rvrt_artifact_thread_count(const rvrt_artifact_t *artifact, uint32_t *count);

rvrt_artifact_status_t rvrt_artifact_thread_root_core_offset(
    const rvrt_artifact_t *artifact, uint32_t thread_index,
    rvrt_artifact_core_offset_t *root_core_offset);

rvrt_artifact_status_t
rvrt_artifact_thread_runtime(const rvrt_artifact_t *artifact,
                             uint32_t thread_index,
                             rvrt_artifact_runtime_t *runtime);

/**
 * @brief Borrow an input mapping for repeated cursor-based frame encoding.
 * @param thread_index Artifact thread that owns input_index.
 * @param input_index Local input-mapping index for that thread.
 * @param view Receives a borrowed view invalidated with artifact backing bytes.
 */
rvrt_artifact_status_t rvrt_artifact_get_input_mapping_view(
    const rvrt_artifact_t *artifact, uint32_t thread_index,
    uint32_t input_index, rvrt_artifact_input_mapping_view_t *view);

/**
 * @brief Borrow a key-sorted output mapping for frame decoding.
 * @param thread_index Artifact thread that owns output_index.
 * @param output_index Local output-mapping index for that thread.
 * @param view Receives a borrowed view invalidated with artifact backing bytes.
 */
rvrt_artifact_status_t rvrt_artifact_get_output_mapping_view(
    const rvrt_artifact_t *artifact, uint32_t thread_index,
    uint32_t output_index, rvrt_artifact_output_mapping_view_t *view);

/**
 * @brief Materialize one input entry from a borrowed input mapping view.
 * @param view Borrowed input mapping returned by the view accessor.
 * @param entry_index Zero-based entry position in view.
 * @param entry Receives frame-address and payload metadata.
 */
rvrt_artifact_status_t rvrt_artifact_input_mapping_entry(
    const rvrt_artifact_input_mapping_view_t *view, uint32_t entry_index,
    rvrt_artifact_input_entry_t *entry);

/**
 * @brief Find an output entry by axon-bit key.
 *
 * Returns OK with found set to false when a received frame is not mapped by
 * this output tensor.
 * @param view Borrowed, key-sorted output mapping.
 * @param axon_bit_idx Decoded frame address used as the lookup key.
 * @param entry Receives entry metadata only when found is true.
 * @param found Receives whether the key exists in view.
 */
rvrt_artifact_status_t rvrt_artifact_output_mapping_find(
    const rvrt_artifact_output_mapping_view_t *view, uint32_t axon_bit_idx,
    rvrt_artifact_output_entry_t *entry, bool *found);

const char *rvrt_artifact_status_string(rvrt_artifact_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_ARTIFACT_READER_H */
