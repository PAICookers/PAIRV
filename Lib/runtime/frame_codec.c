#include "frame_codec.h"
#include "debug.h"
#include <stddef.h>

#define RVRT_DEBUG_TITLE "rvrt"

#define RVRT_WORD_BITS 32U
#define RVRT_U32_MASK 0xFFFFFFFFULL

#define RVRT_HDR_OFFSET 60U
#define RVRT_HDR_MASK 0xFU
#define RVRT_HDR_WORK_TYPE1 8U
#define RVRT_HDR_CTRL_TYPE1 12U
#define RVRT_HDR_CTRL_TYPE2 13U

#define RVRT_SIGN_MAG6_MIN (-31)
#define RVRT_SIGN_MAG6_MAX 31
#define RVRT_SIGN_MAG6_SIGN_BIT 0x20U
#define RVRT_SIGN_MAG6_MASK 0x3FU

#define RVRT_DEST_XY_OFFSET 54U
#define RVRT_DEST_X_OFFSET 48U
#define RVRT_DEST_Y_OFFSET 42U
#define RVRT_COPY_XY_OFFSET 36U
#define RVRT_COPY_X_OFFSET 30U
#define RVRT_COPY_Y_OFFSET 24U

#define RVRT_CTRL_PAYLOAD_MASK 0xFFFFFFU

#define RVRT_OW1_TARGET_LCN_MAX 7U
#define RVRT_OW1_TS_BITS 8U
#define RVRT_OW1_TS_MASK 0xFFU
#define RVRT_OW1_TS_HI_OFFSET 60U
#define RVRT_OW1_TS_HI_MASK 0x1U
#define RVRT_OW1_TS_LO_BITS 7U
#define RVRT_OW1_TS_LO_OFFSET 17U
#define RVRT_OW1_TS_LO_MASK 0x7FU
#define RVRT_OW1_AXON_BITS 9U
#define RVRT_OW1_AXON_OFFSET 8U
#define RVRT_OW1_AXON_MASK 0x1FFU
#define RVRT_OW1_DATA_BITS 8U
#define RVRT_OW1_DATA_MASK 0xFFU

#define RVRT_MEMBRANE_DATA_BITS 32U
#define RVRT_MEMBRANE_PART_BITS 8U
#define RVRT_MEMBRANE_PART_COUNT 4U

#define RVRT_DTYPE_UINT1 1U
#define RVRT_DTYPE_INT1 2U
#define RVRT_DTYPE_UINT2 3U
#define RVRT_DTYPE_INT2 4U
#define RVRT_DTYPE_UINT4 5U
#define RVRT_DTYPE_INT4 6U
#define RVRT_DTYPE_UINT8 7U
#define RVRT_DTYPE_INT8 8U

typedef struct dtype_info {
    uint8_t bits;
    bool is_signed;
} dtype_info_t;

static const dtype_info_t k_dtype_info[] = {
    [RVRT_DTYPE_UINT1] = {1U, false}, [RVRT_DTYPE_INT1] = {1U, true},
    [RVRT_DTYPE_UINT2] = {2U, false}, [RVRT_DTYPE_INT2] = {2U, true},
    [RVRT_DTYPE_UINT4] = {4U, false}, [RVRT_DTYPE_INT4] = {4U, true},
    [RVRT_DTYPE_UINT8] = {8U, false}, [RVRT_DTYPE_INT8] = {8U, true},
};

/** @brief Collapse artifact-reader failures into the codec status domain. */
static rvrt_status_t runtime_status_from_artifact(rvrt_artifact_status_t status)
{
    if (status == RVRT_ARTIFACT_OK) {
        return RVRT_STATUS_OK;
    }
    if (status == RVRT_ARTIFACT_NULL_ARGUMENT) {
        return RVRT_STATUS_NULL_ARGUMENT;
    }
    if (status == RVRT_ARTIFACT_OUT_OF_RANGE) {
        return RVRT_STATUS_OUT_OF_RANGE;
    }
    return RVRT_STATUS_ARTIFACT_ERROR;
}

static uint64_t pack_field(uint32_t value, uint32_t offset, uint32_t mask)
{
    return ((uint64_t)(value & mask)) << offset;
}

static void frame_from_u64(uint64_t value, rvrt_frame_t *frame)
{
    frame->high = (uint32_t)(value >> RVRT_WORD_BITS);
    frame->low = (uint32_t)(value & RVRT_U32_MASK);
}

static uint64_t frame_to_u64(const rvrt_frame_t *frame)
{
    return (((uint64_t)frame->high) << RVRT_WORD_BITS) | (uint64_t)frame->low;
}

static bool sign_magnitude6(int32_t value, uint32_t *encoded)
{
    if ((encoded == NULL) || (value < RVRT_SIGN_MAG6_MIN) ||
        (value > RVRT_SIGN_MAG6_MAX)) {
        return false;
    }

    if (value < 0) {
        *encoded = RVRT_SIGN_MAG6_SIGN_BIT | (uint32_t)(-value);
    } else {
        *encoded = (uint32_t)value;
    }
    return true;
}

static bool pack_zxy(const rvrt_artifact_core_offset_t *offset,
                     uint64_t *packed)
{
    uint32_t xy = 0U;
    uint32_t x = 0U;
    uint32_t y = 0U;
    if ((offset == NULL) || (packed == NULL) ||
        !sign_magnitude6(offset->xy, &xy) || !sign_magnitude6(offset->x, &x) ||
        !sign_magnitude6(offset->y, &y)) {
        return false;
    }

    *packed = pack_field(xy, RVRT_DEST_XY_OFFSET, RVRT_SIGN_MAG6_MASK) |
              pack_field(x, RVRT_DEST_X_OFFSET, RVRT_SIGN_MAG6_MASK) |
              pack_field(y, RVRT_DEST_Y_OFFSET, RVRT_SIGN_MAG6_MASK);
    return true;
}

static bool pack_copy(const rvrt_artifact_copy_count_t *copy, uint64_t *packed)
{
    uint32_t xy = 0U;
    uint32_t x = 0U;
    uint32_t y = 0U;
    if ((copy == NULL) || (packed == NULL) || !sign_magnitude6(copy->xy, &xy) ||
        !sign_magnitude6(copy->x, &x) || !sign_magnitude6(copy->y, &y)) {
        return false;
    }

    *packed = pack_field(xy, RVRT_COPY_XY_OFFSET, RVRT_SIGN_MAG6_MASK) |
              pack_field(x, RVRT_COPY_X_OFFSET, RVRT_SIGN_MAG6_MASK) |
              pack_field(y, RVRT_COPY_Y_OFFSET, RVRT_SIGN_MAG6_MASK);
    return true;
}

static rvrt_status_t frame_dest(uint32_t header,
                                const rvrt_artifact_core_offset_t *offset,
                                const rvrt_artifact_copy_count_t *copy,
                                uint64_t *dest)
{
    uint64_t packed_offset = 0U;
    uint64_t packed_copy = 0U;
    if ((dest == NULL) || !pack_zxy(offset, &packed_offset) ||
        !pack_copy(copy, &packed_copy)) {
        return RVRT_STATUS_BAD_VALUE;
    }

    *dest = pack_field(header, RVRT_HDR_OFFSET, RVRT_HDR_MASK) | packed_offset |
            packed_copy;
    return RVRT_STATUS_OK;
}

/**
 * @brief Build a PAICORE control frame addressed to an artifact thread root.
 *
 * payload occupies the low 24 bits; callers must use a control header value.
 */
static rvrt_status_t build_control_frame(const rvrt_artifact_t *artifact,
                                         uint32_t thread_index, uint32_t header,
                                         uint32_t payload, rvrt_frame_t *frame)
{
    if ((artifact == NULL) || (frame == NULL)) {
        return RVRT_STATUS_NULL_ARGUMENT;
    }
    if (payload > RVRT_CTRL_PAYLOAD_MASK) {
        return RVRT_STATUS_BAD_VALUE;
    }

    rvrt_artifact_core_offset_t root = {0};
    rvrt_artifact_copy_count_t copy = {0};
    rvrt_artifact_status_t artifact_status =
        rvrt_artifact_thread_root_core_offset(artifact, thread_index, &root);
    rvrt_status_t status = runtime_status_from_artifact(artifact_status);
    if (status != RVRT_STATUS_OK) {
        return status;
    }

    uint64_t dest = 0U;
    status = frame_dest(header, &root, &copy, &dest);
    if (status != RVRT_STATUS_OK) {
        return status;
    }

    frame_from_u64(dest | (uint64_t)payload, frame);
    return RVRT_STATUS_OK;
}

static bool dtype_bits(uint32_t dtype, uint32_t *bits, bool *is_signed)
{
    if ((bits == NULL) || (is_signed == NULL)) {
        return false;
    }

    if (dtype >= (sizeof(k_dtype_info) / sizeof(k_dtype_info[0]))) {
        return false;
    }

    const dtype_info_t info = k_dtype_info[dtype];
    if (info.bits == 0U) {
        return false;
    }

    *bits = (uint32_t)info.bits;
    *is_signed = info.is_signed;
    return true;
}

static uint32_t bit_mask(uint32_t bits) { return (1U << bits) - 1U; }

static bool encode_payload(uint8_t raw_value, uint32_t bits, bool is_signed,
                           uint8_t *payload)
{
    if (payload == NULL) {
        return false;
    }

    const uint32_t mask = bit_mask(bits);
    if (!is_signed) {
        if (((uint32_t)raw_value & ~mask) != 0U) {
            return false;
        }
        *payload = raw_value;
        return true;
    }

    const int32_t value = ((raw_value & 0x80U) != 0U)
                              ? (int32_t)raw_value - 0x100
                              : (int32_t)raw_value;
    const int32_t extent = (int32_t)(1U << (bits - 1U));
    if ((value < -extent) || (value >= extent)) {
        return false;
    }
    *payload = (uint8_t)((uint32_t)value & mask);
    return true;
}

static uint8_t decode_payload(uint32_t payload, uint32_t bits, bool is_signed)
{
    const uint32_t mask = bit_mask(bits);
    uint32_t value = payload & mask;
    if (is_signed && ((value & (1U << (bits - 1U))) != 0U)) {
        value |= ~mask;
    }
    return (uint8_t)value;
}

/**
 * @brief Decode a received work frame's timestep and axon address into an
 *        output mapping entry.
 *
 * Shared by both rvrt_decode_output_frame() and rvrt_decode_membrane_frame().
 * Sets *found = false (OK) when the frame's runtime timestep is non-zero or no
 * mapping entry matches the axon_bit_idx; these are valid skip conditions.
 */
static rvrt_status_t find_output_entry_for_work_frame(
    const rvrt_artifact_output_mapping_view_t *view, const rvrt_frame_t *frame,
    rvrt_artifact_output_entry_t *entry, bool *found)
{
    if ((view == NULL) || (view->entries == NULL) || (frame == NULL) ||
        (entry == NULL) || (found == NULL)) {
        return RVRT_STATUS_NULL_ARGUMENT;
    }

    *found = false;
    if (view->target_lcn > RVRT_OW1_TARGET_LCN_MAX) {
        RV_DEBUG_LOGW(RVRT_DEBUG_TITLE, "unsupported output target_lcn=%u",
                      (unsigned)view->target_lcn);
        return RVRT_STATUS_UNSUPPORTED;
    }

    const uint64_t raw_frame = frame_to_u64(frame);
    const uint32_t frame_timestep =
        (uint32_t)((((raw_frame >> RVRT_OW1_TS_HI_OFFSET) & RVRT_OW1_TS_HI_MASK)
                    << RVRT_OW1_TS_LO_BITS) |
                   ((raw_frame >> RVRT_OW1_TS_LO_OFFSET) &
                    RVRT_OW1_TS_LO_MASK));
    const uint32_t runtime_timestep = frame_timestep >> view->target_lcn;
    if (runtime_timestep != 0U) {
        return RVRT_STATUS_OK;
    }

    const uint32_t frame_axon =
        (uint32_t)((raw_frame >> RVRT_OW1_AXON_OFFSET) & RVRT_OW1_AXON_MASK);
    const uint32_t ax_width = RVRT_OW1_AXON_BITS + view->target_lcn;
    const uint32_t axon_bit_idx =
        (((frame_timestep & RVRT_OW1_TS_MASK) << RVRT_OW1_AXON_BITS) |
         frame_axon) &
        ((1U << ax_width) - 1U);

    const rvrt_artifact_status_t artifact_status =
        rvrt_artifact_output_mapping_find(view, axon_bit_idx, entry, found);
    return runtime_status_from_artifact(artifact_status);
}

/** @brief Encode one nonzero input entry as an offline work-type-1 frame. */
static rvrt_status_t build_work1_frame(const rvrt_artifact_input_entry_t *entry,
                                       uint32_t timestep, uint8_t payload,
                                       rvrt_frame_t *frame)
{
    if ((entry == NULL) || (frame == NULL)) {
        return RVRT_STATUS_NULL_ARGUMENT;
    }
    if ((entry->target_lcn > RVRT_OW1_TARGET_LCN_MAX) ||
        (entry->addr_axon > RVRT_OW1_AXON_MASK)) {
        return RVRT_STATUS_BAD_VALUE;
    }

    uint64_t dest = 0U;
    rvrt_status_t status = frame_dest(RVRT_HDR_WORK_TYPE1, &entry->core_offset,
                                      &entry->copy_count, &dest);
    if (status != RVRT_STATUS_OK) {
        return status;
    }

    const uint32_t tick_limit = 1U << entry->target_lcn;
    if (entry->tick_relative >= tick_limit) {
        return RVRT_STATUS_BAD_VALUE;
    }

    const uint32_t timestep_capacity =
        1U << (RVRT_OW1_TS_BITS - entry->target_lcn);
    const uint32_t wrapped_timestep = timestep % timestep_capacity;
    const uint32_t resolved_timestep =
        (wrapped_timestep << entry->target_lcn) + entry->tick_relative;
    if (resolved_timestep > RVRT_OW1_TS_MASK) {
        return RVRT_STATUS_BAD_VALUE;
    }

    const uint64_t frame_addr =
        pack_field(resolved_timestep >> RVRT_OW1_TS_LO_BITS,
                   RVRT_OW1_TS_HI_OFFSET, RVRT_OW1_TS_HI_MASK) |
        pack_field(resolved_timestep, RVRT_OW1_TS_LO_OFFSET,
                   RVRT_OW1_TS_LO_MASK) |
        pack_field(entry->addr_axon, RVRT_OW1_AXON_OFFSET, RVRT_OW1_AXON_MASK);

    frame_from_u64(dest | frame_addr | (uint64_t)payload, frame);
    return RVRT_STATUS_OK;
}

rvrt_status_t rvrt_build_init_frame(const rvrt_artifact_t *artifact,
                                    uint32_t thread_index, rvrt_frame_t *frame)
{
    return build_control_frame(artifact, thread_index, RVRT_HDR_CTRL_TYPE2, 0U,
                               frame);
}

rvrt_status_t rvrt_build_sync_frame(const rvrt_artifact_t *artifact,
                                    uint32_t thread_index, rvrt_frame_t *frame)
{
    rvrt_artifact_runtime_t runtime = {0};
    rvrt_artifact_status_t artifact_status =
        rvrt_artifact_thread_runtime(artifact, thread_index, &runtime);
    rvrt_status_t status = runtime_status_from_artifact(artifact_status);
    if (status != RVRT_STATUS_OK) {
        return status;
    }

    return build_control_frame(artifact, thread_index, RVRT_HDR_CTRL_TYPE1,
                               runtime.sync_steps, frame);
}

void rvrt_input_cursor_init(rvrt_input_cursor_t *cursor, uint32_t timestep)
{
    if (cursor == NULL) {
        return;
    }

    cursor->entry_index = 0U;
    cursor->timestep = timestep;
}

rvrt_status_t
rvrt_encode_input_chunk(const rvrt_artifact_input_mapping_view_t *view,
                        rvrt_input_cursor_t *cursor, const uint8_t *input,
                        uint32_t input_size, rvrt_frame_t *frames,
                        uint32_t frame_capacity, uint32_t *frame_count)
{
    if ((view == NULL) || (view->entries == NULL) || (cursor == NULL) ||
        (input == NULL) || (frames == NULL) || (frame_count == NULL)) {
        return RVRT_STATUS_NULL_ARGUMENT;
    }
    if (frame_capacity == 0U) {
        RV_DEBUG_LOGW(RVRT_DEBUG_TITLE, "input frame capacity is zero");
        return RVRT_STATUS_BAD_VALUE;
    }

    *frame_count = 0U;

    while (cursor->entry_index < view->entry_count) {
        rvrt_artifact_input_entry_t entry = {0};
        const rvrt_artifact_status_t artifact_status =
            rvrt_artifact_input_mapping_entry(view, cursor->entry_index,
                                              &entry);
        rvrt_status_t status = runtime_status_from_artifact(artifact_status);
        if (status != RVRT_STATUS_OK) {
            return status;
        }

        uint32_t entry_bits = 0U;
        bool is_signed = false;
        if (!dtype_bits(entry.dtype, &entry_bits, &is_signed) ||
            (entry_bits != view->bit_width)) {
            RV_DEBUG_LOGW(RVRT_DEBUG_TITLE,
                          "unsupported input dtype=%u bit_width=%u",
                          (unsigned)entry.dtype, (unsigned)view->bit_width);
            return RVRT_STATUS_UNSUPPORTED;
        }
        if (entry.elem_idx >= input_size) {
            return RVRT_STATUS_OUT_OF_RANGE;
        }

        uint8_t payload = 0U;
        if (!encode_payload(input[entry.elem_idx], entry_bits, is_signed,
                            &payload)) {
            RV_DEBUG_LOGW(RVRT_DEBUG_TITLE, "input payload out of range");
            return RVRT_STATUS_BAD_VALUE;
        }

        cursor->entry_index++;
        if (payload == 0U) {
            continue;
        }
        if (*frame_count >= frame_capacity) {
            cursor->entry_index--;
            return RVRT_STATUS_BUFFER_FULL;
        }

        status = build_work1_frame(&entry, cursor->timestep, payload,
                                   &frames[*frame_count]);
        if (status != RVRT_STATUS_OK) {
            return status;
        }
        (*frame_count)++;
    }

    return RVRT_STATUS_DONE;
}

rvrt_status_t
rvrt_decode_output_frame(const rvrt_artifact_output_mapping_view_t *view,
                         const rvrt_frame_t *frame, uint8_t *output,
                         uint32_t output_size, bool *written)
{
    if ((view == NULL) || (view->entries == NULL) || (frame == NULL) ||
        (output == NULL) || (written == NULL)) {
        return RVRT_STATUS_NULL_ARGUMENT;
    }

    *written = false;
    if (!rvrt_frame_is_work_type1(frame)) {
        return RVRT_STATUS_OK;
    }

    if (view->kind != RVRT_OUTPUT_DATA) {
        RV_DEBUG_LOGW(RVRT_DEBUG_TITLE, "unsupported output kind=%u",
                      (unsigned)view->kind);
        return RVRT_STATUS_UNSUPPORTED;
    }

    if ((view->bit_width == 0U) || (view->bit_width > RVRT_OW1_DATA_BITS)) {
        RV_DEBUG_LOGW(RVRT_DEBUG_TITLE, "unsupported output bit_width=%u",
                      (unsigned)view->bit_width);
        return RVRT_STATUS_UNSUPPORTED;
    }

    rvrt_artifact_output_entry_t entry = {0};
    bool found = false;
    rvrt_status_t status =
        find_output_entry_for_work_frame(view, frame, &entry, &found);
    if (status != RVRT_STATUS_OK) {
        return status;
    }
    if (!found) {
        return RVRT_STATUS_OK;
    }

    uint32_t entry_bits = 0U;
    bool is_signed = false;
    if (!dtype_bits(entry.dtype, &entry_bits, &is_signed) ||
        (entry_bits != view->bit_width)) {
        RV_DEBUG_LOGW(RVRT_DEBUG_TITLE,
                      "unsupported output dtype=%u bit_width=%u",
                      (unsigned)entry.dtype, (unsigned)view->bit_width);
        return RVRT_STATUS_UNSUPPORTED;
    }
    if (entry.elem_idx >= output_size) {
        return RVRT_STATUS_OUT_OF_RANGE;
    }

    const uint32_t payload = frame->low & RVRT_OW1_DATA_MASK;
    if (!is_signed && ((payload & ~bit_mask(entry_bits)) != 0U)) {
        return RVRT_STATUS_OK;
    }

    output[entry.elem_idx] = decode_payload(payload, entry_bits, is_signed);
    *written = true;
    return RVRT_STATUS_OK;
}

rvrt_status_t rvrt_decode_membrane_frame(
    const rvrt_artifact_output_mapping_view_t *view, const rvrt_frame_t *frame,
    int32_t *output, uint32_t output_size,
    rvrt_membrane_decode_state_t *state, uint32_t state_size, bool *written)
{
    if ((view == NULL) || (view->entries == NULL) || (frame == NULL) ||
        (output == NULL) || (state == NULL) || (written == NULL)) {
        return RVRT_STATUS_NULL_ARGUMENT;
    }

    *written = false;
    if (!rvrt_frame_is_work_type2(frame)) {
        return RVRT_STATUS_OK;
    }

    if (view->kind != RVRT_OUTPUT_VOLTAGE) {
        RV_DEBUG_LOGW(RVRT_DEBUG_TITLE, "unsupported membrane output kind=%u",
                      (unsigned)view->kind);
        return RVRT_STATUS_UNSUPPORTED;
    }
    if (view->bit_width != RVRT_MEMBRANE_DATA_BITS) {
        RV_DEBUG_LOGW(RVRT_DEBUG_TITLE,
                      "unsupported membrane output bit_width=%u",
                      (unsigned)view->bit_width);
        return RVRT_STATUS_UNSUPPORTED;
    }

    rvrt_artifact_output_entry_t entry = {0};
    bool found = false;
    rvrt_status_t status =
        find_output_entry_for_work_frame(view, frame, &entry, &found);
    if (status != RVRT_STATUS_OK) {
        return status;
    }
    if (!found) {
        return RVRT_STATUS_OK;
    }
    if ((entry.elem_idx >= output_size) || (entry.elem_idx >= state_size)) {
        return RVRT_STATUS_OUT_OF_RANGE;
    }

    rvrt_membrane_decode_state_t *const slot = &state[entry.elem_idx];
    if (slot->parts_received >= RVRT_MEMBRANE_PART_COUNT) {
        return RVRT_STATUS_BAD_VALUE;
    }

    const uint32_t payload = frame->low & RVRT_OW1_DATA_MASK;
    slot->value |= payload << (slot->parts_received * RVRT_MEMBRANE_PART_BITS);
    slot->parts_received++;

    if (slot->parts_received == RVRT_MEMBRANE_PART_COUNT) {
        output[entry.elem_idx] = (int32_t)slot->value;
        slot->value = 0U;
        slot->parts_received = 0U;
        *written = true;
    }

    return RVRT_STATUS_OK;
}

const char *rvrt_status_string(rvrt_status_t status)
{
    switch (status) {
        case RVRT_STATUS_OK:
            return "ok";
        case RVRT_STATUS_DONE:
            return "done";
        case RVRT_STATUS_BUFFER_FULL:
            return "buffer full";
        case RVRT_STATUS_NULL_ARGUMENT:
            return "null argument";
        case RVRT_STATUS_ARTIFACT_ERROR:
            return "artifact error";
        case RVRT_STATUS_OUT_OF_RANGE:
            return "out of range";
        case RVRT_STATUS_BAD_VALUE:
            return "bad value";
        case RVRT_STATUS_UNSUPPORTED:
            return "unsupported";
        default:
            return "unknown";
    }
}
