#ifndef RVRT_ARTIFACT_READER_H
#define RVRT_ARTIFACT_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Required alignment in bytes for artifact backing storage. */
#define RVRT_ARTIFACT_ALIGNMENT 8U
/** Maximum tensor rank materialized into fixed-size public arrays. */
#define RVRT_ARTIFACT_MAX_RANK 8U
/** Execution-stage kind value selecting a PAICORE phase. */
#define RVRT_STAGE_PAICORE 0U
/** Execution-stage kind value selecting a generated CPU task. */
#define RVRT_STAGE_CPU_TASK 1U

/** @brief Result of validating or reading an exported runtime artifact. */
typedef enum rvrt_artifact_status_e {
    /** Requested value was returned. */
    RVRT_ARTIFACT_OK = 0,
    /** A required pointer was NULL. */
    RVRT_ARTIFACT_NULL_ARGUMENT = 1,
    /** FlatBuffers verification failed. */
    RVRT_ARTIFACT_VERIFY_FAILED = 2,
    /** Required artifact field is absent. */
    RVRT_ARTIFACT_MISSING_FIELD = 3,
    /** Requested index or size is invalid. */
    RVRT_ARTIFACT_OUT_OF_RANGE = 4,
    /** Backing bytes lack required alignment. */
    RVRT_ARTIFACT_BAD_ALIGNMENT = 5,
    /** Value is unsupported or inconsistent. */
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
    /** CompileArtifacts schema version. */
    uint32_t schema_version;
    /** Number of raw 32-bit config words. */
    uint32_t config_word_count;
    /** Serialized ConfigFrames word-order enum. */
    uint32_t config_word_order;
    /** Number of entries in IOMapping.threads. */
    uint32_t thread_count;
} rvrt_artifact_info_t;

/**
 * @brief Logical buffer and frame capacities needed by one selected thread.
 *
 * Tensor byte counts are logical sizes. Callers may provide larger physical
 * buffers, but not smaller ones.
 */
typedef struct rvrt_artifact_capacity_s {
    /** Logical bytes required by the first stage input. */
    uint32_t input_bytes;
    /** Logical bytes produced by the final stage. */
    uint32_t final_output_bytes;
    /** Suggested RX frame capacity, including margin. */
    uint32_t rx_frame_count;
    /** Suggested input encoding workspace frames. */
    uint32_t workspace_frame_count;
    /** DataType code of the final output tensor. */
    uint32_t final_output_dtype;
    /** Number of valid final_output_shape entries. */
    uint32_t final_output_rank;
    /** Final tensor shape. */
    int32_t final_output_shape[RVRT_ARTIFACT_MAX_RANK];
} rvrt_artifact_capacity_t;

/**
 * @brief Target package identity and generated-task ABI requirement.
 *
 * String pointers borrow FlatBuffers storage and remain valid only while the
 * artifact backing bytes remain alive and unchanged.
 */
typedef struct rvrt_artifact_runtime_target_s {
    /** NUL-terminated target package identifier. */
    const char *target_id;
    /** NUL-terminated target profile identifier. */
    const char *profile_id;
    /** Generated CPU-task ABI version. */
    uint32_t required_task_abi_version;
} rvrt_artifact_runtime_target_t;

/** @brief Relative NoC destination coordinates from a thread root core. */
typedef struct rvrt_artifact_core_offset_s {
    /** Relative XY routing coordinate. */
    int32_t xy;
    /** Relative X routing coordinate. */
    int32_t x;
    /** Relative Y routing coordinate. */
    int32_t y;
} rvrt_artifact_core_offset_t;

/** @brief NoC multicast copy counts for the three routing dimensions. */
typedef struct rvrt_artifact_copy_count_s {
    /** Number of copies in the XY dimension. */
    int32_t xy;
    /** Number of copies in the X dimension. */
    int32_t x;
    /** Number of copies in the Y dimension. */
    int32_t y;
} rvrt_artifact_copy_count_t;

/** @brief Static runtime timing metadata for one artifact thread. */
typedef struct rvrt_artifact_runtime_s {
    /** Number of application input/output timesteps. */
    uint32_t timesteps;
    /** PAICORE pipeline depth in ticks. */
    uint32_t tick_depth;
    /** Compiler-selected PAICORE synchronization payload. */
    uint32_t sync_steps;
    /** DecodeMode enum serialized by the artifact. */
    uint32_t decode_mode;
} rvrt_artifact_runtime_t;

/** DecodeMode value for a continuous output frame stream. */
#define RVRT_DECODE_MODE_STREAM 0U
/** DecodeMode value for per-step output delivery. */
#define RVRT_DECODE_MODE_STEP 1U

/** @brief One logical input element encoded as an offline work frame. */
typedef struct rvrt_artifact_input_entry_s {
    /** Logical input tensor element index. */
    uint32_t elem_idx;
    /** Destination relative to thread root. */
    rvrt_artifact_core_offset_t core_offset;
    /** Multicast copy counts. */
    rvrt_artifact_copy_count_t copy_count;
    /** Tick-relative address component. */
    uint32_t tick_relative;
    /** Axon address component. */
    uint32_t addr_axon;
    /** Target LCN used to pack the work-frame address. */
    uint32_t target_lcn;
    /** Copy identifier for the mapped destination. */
    uint32_t copy_id;
    /** DataType enum used to encode the payload. */
    uint32_t dtype;
} rvrt_artifact_input_entry_t;

/** @brief One output-frame address mapped to a logical tensor element. */
typedef struct rvrt_artifact_output_entry_s {
    /** Logical output tensor element index. */
    uint32_t elem_idx;
    /** Mapped output copy identifier. */
    uint32_t copy_id;
    /** Flat axon-bit lookup key for received frames. */
    uint32_t axon_bit_idx;
} rvrt_artifact_output_entry_t;

/**
 * @brief Borrowed input-entry vector for streaming input encoding.
 *
 * Views borrow FlatBuffers storage owned by the artifact backing bytes.
 */
typedef struct rvrt_artifact_input_mapping_view_s {
    /** Opaque borrowed FlatBuffers input-entry vector. */
    const void *entries;
    /** Number of entries accepted by the accessor API. */
    uint32_t entry_count;
    /** Declared logical input bit width. */
    uint32_t bit_width;
} rvrt_artifact_input_mapping_view_t;

/**
 * @brief Borrowed, key-sorted output-entry vector for frame decoding.
 *
 * Do not dereference entries directly; use rvrt_artifact_output_mapping_find().
 */
typedef struct rvrt_artifact_output_mapping_view_s {
    /** Opaque borrowed FlatBuffers output-entry vector. */
    const void *entries;
    /** Number of frame-address mapping entries. */
    uint32_t entry_count;
    /** Validated number of logical output elements. */
    uint32_t element_count;
    /** DataType enum of the logical output tensor. */
    uint32_t dtype;
    /** OutputKind enum, such as DATA or VOLTAGE. */
    uint32_t kind;
    /** Number of target-LCN bits in a frame timestamp. */
    uint32_t target_lcn;
} rvrt_artifact_output_mapping_view_t;

/** @brief Generated CPU task input and output runtime-buffer references. */
typedef struct rvrt_artifact_cpu_task_s {
    /** ExecutionPlan buffer index consumed by the task. */
    uint32_t input_ref;
    /** ExecutionPlan buffer index produced by the task. */
    uint32_t output_ref;
} rvrt_artifact_cpu_task_t;

/** @brief One ordered execution-plan stage and its local table reference. */
typedef struct rvrt_artifact_stage_s {
    /** Compiler-assigned order identifier. */
    uint32_t stage_index;
    /** RVRT_STAGE_PAICORE or RVRT_STAGE_CPU_TASK. */
    uint32_t kind;
    /** Index in the table selected by kind. */
    uint32_t ref_index;
} rvrt_artifact_stage_t;

/**
 * @brief PAICORE stage metadata used to encode, synchronize, and decode a pass.
 *
 * latency_ticks is a positive 24-bit control-frame payload computed by the
 * compiler.
 */
typedef struct rvrt_artifact_paicore_phase_s {
    /** ExecutionPlan input runtime-buffer index. */
    uint32_t input_ref;
    /** ExecutionPlan output runtime-buffer index. */
    uint32_t output_ref;
    /** Thread-local input mapping index. */
    uint32_t input_mapping_ref;
    /** Thread-local output mapping index. */
    uint32_t output_mapping_ref;
    /** Positive 24-bit PAICORE synchronization payload. */
    uint32_t latency_ticks;
} rvrt_artifact_paicore_phase_t;

/**
 * @brief Verify an artifact buffer and bind non-owning FlatBuffers handles.
 *
 * Checks the buffer alignment, FlatBuffers layout, supported schema version,
 * root runtime tables, and I/O mapping invariants. On failure after argument
 * validation, clears artifact so it cannot retain partially bound handles.
 * @param data Artifact byte buffer aligned to RVRT_ARTIFACT_ALIGNMENT bytes.
 * @param size Nonzero byte length of data.
 * @param artifact Receives verified root, I/O mapping, and config-frame
 * handles.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_BAD_ALIGNMENT for an
 *         unaligned buffer; RVRT_ARTIFACT_VERIFY_FAILED for invalid
 *         FlatBuffers data; or a missing-field/value status for an unsupported
 *         artifact contract.
 */
rvrt_artifact_status_t rvrt_artifact_read(const uint8_t *data, size_t size,
                                          rvrt_artifact_t *artifact);

/**
 * @brief Read basic metadata without materializing runtime buffers.
 * @param artifact Verified artifact returned by rvrt_artifact_read().
 * @param info Receives schema version, config word count/order, and thread
 * count.
 * @return RVRT_ARTIFACT_OK on success; otherwise a null-argument or artifact
 *         status propagated while reading the required tables.
 */
rvrt_artifact_status_t rvrt_artifact_get_info(const rvrt_artifact_t *artifact,
                                              rvrt_artifact_info_t *info);

/**
 * @brief Derive execution-plan storage recommendations for one artifact thread.
 *
 * Uses the first and final plan stages for logical input/output tensor sizes,
 * then scans all PAICORE phases with mappings owned by thread_index. The RX
 * count includes the runtime completion margin; workspace_frame_count is
 * capped by the runtime's compile-time recommendation. This accessor requires
 * a nonempty ExecutionPlan with at least one PAICORE phase and is not needed
 * by manual-session applications that choose their own capacities.
 * @param artifact Verified artifact backing every returned value.
 * @param thread_index Thread that owns PAICORE phase mapping references.
 * @param capacity Receives zero-initialized logical sizes and frame counts.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_MISSING_FIELD when the
 *         required plan/phase/vector is absent; RVRT_ARTIFACT_OUT_OF_RANGE for
 *         an overflowing capacity calculation; or an underlying accessor
 *         status for an invalid index or tensor contract.
 */
rvrt_artifact_status_t
rvrt_artifact_get_capacity(const rvrt_artifact_t *artifact,
                           uint32_t thread_index,
                           rvrt_artifact_capacity_t *capacity);

/**
 * @brief Read target package identity and generated CPU-task ABI requirement.
 *
 * target_id and profile_id borrow NUL-terminated strings from the artifact.
 * @param artifact Verified artifact containing an ExecutionPlan.
 * @param target Receives borrowed identity strings and required ABI version.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_MISSING_FIELD when the
 *         plan or identity fields are absent; otherwise an artifact status.
 */
rvrt_artifact_status_t
rvrt_artifact_runtime_target(const rvrt_artifact_t *artifact,
                             rvrt_artifact_runtime_target_t *target);

/**
 * @brief Count generated CPU tasks in an artifact execution plan.
 * @param artifact Verified artifact containing an ExecutionPlan.
 * @param count Receives the task count; receives zero when the task vector is
 * absent.
 * @return RVRT_ARTIFACT_OK on success; otherwise an artifact or null-argument
 *         status.
 */
rvrt_artifact_status_t
rvrt_artifact_cpu_task_count(const rvrt_artifact_t *artifact, uint32_t *count);

/**
 * @brief Read one generated CPU task's runtime-buffer references.
 * @param artifact Verified artifact containing an ExecutionPlan.
 * @param task_index Zero-based index less than rvrt_artifact_cpu_task_count().
 * @param task Receives input_ref and output_ref for the selected task.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_OUT_OF_RANGE for an
 *         invalid task index; or an artifact/null-argument status.
 */
rvrt_artifact_status_t rvrt_artifact_cpu_task(const rvrt_artifact_t *artifact,
                                              uint32_t task_index,
                                              rvrt_artifact_cpu_task_t *task);

/**
 * @brief Count ordered execution stages in an artifact execution plan.
 * @param artifact Verified artifact containing an ExecutionPlan.
 * @param count Receives the stage count; receives zero when the stage vector is
 * absent.
 * @return RVRT_ARTIFACT_OK on success; otherwise an artifact or null-argument
 *         status.
 */
rvrt_artifact_status_t
rvrt_artifact_stage_count(const rvrt_artifact_t *artifact, uint32_t *count);

/**
 * @brief Read one ordered execution stage.
 * @param artifact Verified artifact containing an ExecutionPlan.
 * @param stage_index Zero-based index less than rvrt_artifact_stage_count().
 * @param stage Receives the compiler stage identifier, kind, and table index.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_OUT_OF_RANGE for an
 *         invalid stage index; or an artifact/null-argument status.
 */
rvrt_artifact_status_t rvrt_artifact_stage(const rvrt_artifact_t *artifact,
                                           uint32_t stage_index,
                                           rvrt_artifact_stage_t *stage);

/**
 * @brief Resolve a stage's input and output runtime-buffer references.
 *
 * Resolves ref_index through the PAICORE phase or CPU task table selected by
 * stage->kind. The stage must be obtained from rvrt_artifact_stage() for the
 * same artifact.
 * @param artifact Verified artifact that owns stage.
 * @param stage Stage obtained from rvrt_artifact_stage().
 * @param input_ref Receives the stage input buffer index; must not be NULL.
 * @param output_ref Receives the stage output buffer index; must not be NULL.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_BAD_VALUE for an unknown
 *         stage kind; or an artifact/null-argument status.
 */
rvrt_artifact_status_t
rvrt_artifact_stage_buffer_refs(const rvrt_artifact_t *artifact,
                                const rvrt_artifact_stage_t *stage,
                                uint32_t *input_ref, uint32_t *output_ref);

/**
 * @brief Count PAICORE phases in an artifact execution plan.
 * @param artifact Verified artifact containing an ExecutionPlan.
 * @param count Receives the phase count; receives zero when the phase vector is
 * absent.
 * @return RVRT_ARTIFACT_OK on success; otherwise an artifact or null-argument
 *         status.
 */
rvrt_artifact_status_t
rvrt_artifact_paicore_phase_count(const rvrt_artifact_t *artifact,
                                  uint32_t *count);

/**
 * @brief Read one PAICORE phase, including its validated sync latency.
 * @param artifact Verified artifact containing an ExecutionPlan.
 * @param phase_index Zero-based index less than
 * rvrt_artifact_paicore_phase_count().
 * @param phase Receives buffer refs, thread-local mapping refs, and latency.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_BAD_VALUE when latency is
 *         zero; RVRT_ARTIFACT_OUT_OF_RANGE when it exceeds the 24-bit control
 *         payload; or an artifact/null-argument status.
 */
rvrt_artifact_status_t
rvrt_artifact_paicore_phase(const rvrt_artifact_t *artifact,
                            uint32_t phase_index,
                            rvrt_artifact_paicore_phase_t *phase);

/**
 * @brief Count runtime buffers in an artifact execution plan.
 * @param artifact Verified artifact containing an ExecutionPlan.
 * @param count Receives the buffer count; receives zero when the buffer vector
 * is absent.
 * @return RVRT_ARTIFACT_OK on success; otherwise an artifact or null-argument
 *         status.
 */
rvrt_artifact_status_t
rvrt_artifact_runtime_buffer_count(const rvrt_artifact_t *artifact,
                                   uint32_t *count);

/**
 * @brief Derive a runtime buffer's logical byte size and tensor metadata.
 *
 * Any output pointer may be NULL when that field is not needed. When shape is
 * non-NULL, the caller provides storage for RVRT_ARTIFACT_MAX_RANK dimensions;
 * only the first returned rank entries are valid.
 * @param artifact Verified artifact containing an ExecutionPlan.
 * @param buffer_index Zero-based index less than
 * rvrt_artifact_runtime_buffer_count().
 * @param bytes Optional logical byte-count output.
 * @param dtype Optional DataType numeric-code output.
 * @param rank Optional shape-rank output.
 * @param shape Optional RVRT_ARTIFACT_MAX_RANK dimension output.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_OUT_OF_RANGE for an
 *         invalid buffer index or unrepresentable tensor size; or an artifact
 *         status for an invalid dtype or shape.
 */
rvrt_artifact_status_t rvrt_artifact_runtime_buffer_bytes(
    const rvrt_artifact_t *artifact, uint32_t buffer_index, uint32_t *bytes,
    uint32_t *dtype, uint32_t *rank, int32_t *shape);

/**
 * @brief Count raw 32-bit configuration words in ConfigFrames.words.
 * @param artifact Verified artifact containing config frames.
 * @param count Receives the raw word count, not the logical frame count.
 * @return RVRT_ARTIFACT_OK on success; otherwise an artifact or null-argument
 *         status.
 */
rvrt_artifact_status_t
rvrt_artifact_config_word_count(const rvrt_artifact_t *artifact,
                                uint32_t *count);

/**
 * @brief Read one logical configuration frame in canonical high/low order.
 *
 * Converts the artifact's serialized word_order, so callers always receive
 * high as bits [63:32] and low as bits [31:0].
 * @param artifact Verified artifact containing config frames.
 * @param frame_index Zero-based logical frame index, not a raw word index.
 * @param high Receives logical bits [63:32]; must not be NULL.
 * @param low Receives logical bits [31:0]; must not be NULL.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_OUT_OF_RANGE when
 *         frame_index has no complete word pair; RVRT_ARTIFACT_BAD_VALUE for
 *         an unsupported word-order enum; or an artifact/null-argument status.
 */
rvrt_artifact_status_t
rvrt_artifact_config_frame_words(const rvrt_artifact_t *artifact,
                                 uint32_t frame_index, uint32_t *high,
                                 uint32_t *low);

/**
 * @brief Count artifact I/O threads.
 * @param artifact Verified artifact containing IOMapping.threads.
 * @param count Receives the number of selectable thread indices.
 * @return RVRT_ARTIFACT_OK on success; otherwise an artifact or null-argument
 *         status.
 */
rvrt_artifact_status_t
rvrt_artifact_thread_count(const rvrt_artifact_t *artifact, uint32_t *count);

/**
 * @brief Read the root-core-relative NoC offset for one I/O thread.
 * @param artifact Verified artifact containing IOMapping.threads.
 * @param thread_index Zero-based index less than rvrt_artifact_thread_count().
 * @param root_core_offset Receives the thread root-core offset; must not be
 * NULL.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_OUT_OF_RANGE for an
 *         invalid thread index; or an artifact/null-argument status.
 */
rvrt_artifact_status_t rvrt_artifact_thread_root_core_offset(
    const rvrt_artifact_t *artifact, uint32_t thread_index,
    rvrt_artifact_core_offset_t *root_core_offset);

/**
 * @brief Read static timing and decode metadata for one I/O thread.
 * @param artifact Verified artifact containing IOMapping.threads.
 * @param thread_index Zero-based index less than rvrt_artifact_thread_count().
 * @param runtime Receives timesteps, tick depth, sync payload, and decode mode.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_OUT_OF_RANGE for an
 *         invalid thread index; RVRT_ARTIFACT_MISSING_FIELD when runtime is
 *         absent; or an artifact/null-argument status.
 */
rvrt_artifact_status_t
rvrt_artifact_thread_runtime(const rvrt_artifact_t *artifact,
                             uint32_t thread_index,
                             rvrt_artifact_runtime_t *runtime);

/**
 * @brief Borrow an input mapping for repeated cursor-based frame encoding.
 *
 * Clears view before lookup. The returned view is read-only and becomes invalid
 * when its artifact backing bytes are released, moved, or overwritten.
 * @param artifact Verified artifact containing IOMapping.threads.
 * @param thread_index Artifact thread that owns input_index.
 * @param input_index Thread-local input-mapping index.
 * @param view Receives a borrowed opaque entry vector, entry count, and bit
 * width.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_OUT_OF_RANGE for an
 *         invalid thread/mapping index; RVRT_ARTIFACT_MISSING_FIELD when the
 *         mapping entries are absent; or an artifact/null-argument status.
 */
rvrt_artifact_status_t rvrt_artifact_get_input_mapping_view(
    const rvrt_artifact_t *artifact, uint32_t thread_index,
    uint32_t input_index, rvrt_artifact_input_mapping_view_t *view);

/**
 * @brief Borrow a key-sorted output mapping for frame decoding.
 *
 * Clears view before lookup. The returned view includes the output shape's
 * validated element count and is read-only until its artifact backing bytes are
 * released, moved, or overwritten.
 * @param artifact Verified artifact containing IOMapping.threads.
 * @param thread_index Artifact thread that owns output_index.
 * @param output_index Thread-local output-mapping index.
 * @param view Receives a borrowed opaque entry vector and output metadata.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_OUT_OF_RANGE for an
 *         invalid thread/mapping index; RVRT_ARTIFACT_MISSING_FIELD when
 *         required entries/mapping metadata are absent; or an artifact status
 *         for an invalid output shape.
 */
rvrt_artifact_status_t rvrt_artifact_get_output_mapping_view(
    const rvrt_artifact_t *artifact, uint32_t thread_index,
    uint32_t output_index, rvrt_artifact_output_mapping_view_t *view);

/**
 * @brief Materialize one input entry from a borrowed input mapping view.
 * @param view Borrowed input mapping returned by the view accessor.
 * @param entry_index Zero-based index less than view->entry_count.
 * @param entry Receives frame-address and payload metadata; must not be NULL.
 * @return RVRT_ARTIFACT_OK on success; RVRT_ARTIFACT_OUT_OF_RANGE for an
 *         invalid entry index; RVRT_ARTIFACT_MISSING_FIELD for an absent entry
 *         or routing field; or an artifact/null-argument status.
 */
rvrt_artifact_status_t rvrt_artifact_input_mapping_entry(
    const rvrt_artifact_input_mapping_view_t *view, uint32_t entry_index,
    rvrt_artifact_input_entry_t *entry);

/**
 * @brief Find an output entry by its decoded axon-bit address.
 *
 * Returns success with found set to false when a received frame does not map
 * to this tensor. In that case entry is not modified.
 * @param view Borrowed, key-sorted output mapping.
 * @param axon_bit_idx Decoded frame address used as the lookup key.
 * @param entry Receives entry metadata only when found is true; must not be
 * NULL.
 * @param found Receives whether the key exists in view; must not be NULL.
 * @return RVRT_ARTIFACT_OK on a match or a valid non-match; otherwise a
 *         null-argument status.
 */
rvrt_artifact_status_t rvrt_artifact_output_mapping_find(
    const rvrt_artifact_output_mapping_view_t *view, uint32_t axon_bit_idx,
    rvrt_artifact_output_entry_t *entry, bool *found);

/**
 * @brief Return a static diagnostic string for an artifact status code.
 * @param status Artifact status returned by this API family.
 * @return NUL-terminated static string; returns "unknown" for an unrecognized
 *         numeric value. The caller must not free or modify the string.
 */
const char *rvrt_artifact_status_string(rvrt_artifact_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_ARTIFACT_READER_H */
