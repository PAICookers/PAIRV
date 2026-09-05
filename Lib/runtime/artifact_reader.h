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
    /** PAICORE pipeline latency measured in logical timesteps. */
    uint32_t pipeline_latency;
    /** Artifact-defined PAICORE timeline target that completes this sample. */
    uint32_t completion_sync_timestep;
    /** Output-time encoding enum serialized by the artifact. */
    uint32_t output_time_encoding;
} rvrt_artifact_runtime_t;

/** Output-time encoding value for a continuous output frame stream. */
#define RVRT_OUTPUT_TIME_ENCODING_STREAM 0U
/** Output-time encoding value for per-step output delivery. */
#define RVRT_OUTPUT_TIME_ENCODING_STEP 1U

/**
 * Runtime-only dtype for VOLTAGE output decoded as signed int32.
 *
 * PAIBox leaves the serialized FBS DataType unset for VOLTAGE output; this
 * value is synthesized by the artifact reader and is not an FBS enum value.
 */
#define RVRT_DTYPE_VOLTAGE_INT32 9U

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
    /** Validated number of logical input elements. */
    uint32_t element_count;
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
    /** Runtime dtype code of the logical output tensor. */
    uint32_t dtype;
    /** OutputKind enum, such as DATA or VOLTAGE. */
    uint32_t kind;
    /** Number of target-LCN bits in a frame timestamp. */
    uint32_t target_lcn;
} rvrt_artifact_output_mapping_view_t;

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
 * @param runtime Receives timesteps, pipeline latency, completion target, and
 * output-time encoding. Serialized FlatBuffer field names remain unchanged.
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
 * @param view Receives a borrowed opaque entry vector, entry count, logical
 * element count, and bit width.
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
