#include "paicore_runner_internal.h"

#include <string.h>

#include "debug.h"
#include "frame_codec_internal.h"
#include "session_internal.h"
#include "session_io.h"
#include "session_io_internal.h"

#define RUN_VOLT_LANE_ADDR_SHIFT 3U
#define RUN_VOLT_LANE_STRIDE 8U
#define RUN_VOLT_GROUP_SIZE 8U
#define RUN_VOLT_GROUP_PITCH 32U
#define RUN_VOLT_GROUP_SIZE_SHIFT 3U
#define RUN_VOLT_GROUP_PITCH_SHIFT 5U
#define RUN_DATA_UINT1 1U
#define RUN_INPUT_INT8 8U

static rvrt_session_status_t
runner_session_failure(const char *operation, uint32_t completed_timesteps,
                       rvrt_session_status_t status)
{
#if !RV_DEBUG_ENABLE_LOGGING
    (void)operation;
    (void)completed_timesteps;
#endif
    RV_DEBUG_LOGE("paicore_runner", "%s completed_timesteps=%u failed: %s",
                  operation, (unsigned)completed_timesteps,
                  rvrt_session_status_string(status));
    return status;
}

typedef struct runner_decode_context_s {
    rvrt_paicore_runner_t *runner;
    uint8_t *output;
    size_t output_capacity;
    size_t output_stride;
    uint32_t submitted_timesteps;
    uint32_t covered_slots;
    bool complete_seen;
} runner_decode_context_t;

static rvrt_session_status_t
runner_validate_frame_timestep(const runner_decode_context_t *decode,
                               const rvrt_frame_t *frame)
{
    if (!rvrt_frame_is_work(frame)) {
        return RVRT_SESSION_OK;
    }
    uint32_t timestep = 0U;
    uint32_t axon_bit_idx = 0U;
    if (rvrt_output_frame_address(&decode->runner->output_view, frame,
                                  &timestep,
                                  &axon_bit_idx) != RVRT_CODEC_STATUS_OK) {
        return RVRT_SESSION_OK;
    }
    (void)axon_bit_idx;
    return (timestep < decode->submitted_timesteps)
               ? RVRT_SESSION_OK
               : RVRT_SESSION_RUNTIME_ERROR;
}

static rvrt_session_status_t
configure_fast_layout(rvrt_paicore_runner_t *runner)
{
    runner->has_fast_data_layout = false;
    runner->has_fast_voltage_layout = false;
    runner->fast_output_base = 0U;
    const bool is_data = (runner->output_view.kind == RVRT_OUTPUT_DATA) &&
                         (runner->output_view.dtype == RUN_DATA_UINT1);
    const bool is_voltage =
        (runner->output_view.kind == RVRT_OUTPUT_VOLTAGE) &&
        (runner->output_view.dtype == RVRT_DTYPE_VOLTAGE_INT32);
    if ((!is_data && !is_voltage) ||
        (runner->output_view.entry_count !=
         runner->output_view.element_count) ||
        (runner->output_view.element_count == 0U)) {
        return RVRT_SESSION_OK;
    }

    const uint32_t address_capacity =
        UINT32_C(1) << (RVRT_WF_TS_LO_OFFSET - RVRT_WF_AX_OFFSET +
                        runner->output_view.target_lcn);
    uint32_t base = 0U;
    bool base_found = false;
    for (uint32_t address = 0U; address < address_capacity; ++address) {
        rvrt_artifact_output_entry_t entry = {0};
        bool found = false;
        if (rvrt_artifact_output_mapping_find(&runner->output_view, address,
                                              &entry,
                                              &found) != RVRT_ARTIFACT_OK) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        if (found) {
            if (entry.elem_idx != 0U) {
                return RVRT_SESSION_OK;
            }
            base = address;
            base_found = true;
            break;
        }
    }
    if (!base_found ||
        (is_voltage && ((base & (RUN_VOLT_GROUP_PITCH - 1U)) != 0U))) {
        return RVRT_SESSION_OK;
    }

    for (uint32_t element = 0U; element < runner->output_view.element_count;
         ++element) {
        rvrt_artifact_output_entry_t entry = {0};
        bool found = false;
        const uint32_t offset = is_voltage
                                    ? ((element >> RUN_VOLT_LANE_ADDR_SHIFT)
                                       << RUN_VOLT_GROUP_PITCH_SHIFT) |
                                          (element & (RUN_VOLT_GROUP_SIZE - 1U))
                                    : element;
        if ((base > UINT32_MAX - offset) ||
            (base + offset >= address_capacity)) {
            return RVRT_SESSION_OK;
        }
        const uint32_t expected_axon = base + offset;
        if (rvrt_artifact_output_mapping_find(&runner->output_view,
                                              expected_axon, &entry,
                                              &found) != RVRT_ARTIFACT_OK) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        if (!found) {
            return RVRT_SESSION_OK;
        }
        if ((entry.elem_idx != element) ||
            (entry.axon_bit_idx != expected_axon)) {
            return RVRT_SESSION_OK;
        }
    }

    runner->has_fast_data_layout = is_data;
    runner->has_fast_voltage_layout = is_voltage;
    runner->fast_output_base = base;
    return RVRT_SESSION_OK;
}

static inline rvrt_codec_status_t
runner_decode_data_fast_frame(const rvrt_paicore_runner_t *runner,
                              const rvrt_frame_t *frame, uint8_t *output,
                              size_t output_capacity, size_t output_stride,
                              uint32_t submitted_timesteps)
{
    if (!rvrt_frame_is_work(frame) ||
        (((frame->high >> RVRT_FRAME_WORK_KIND_OFFSET) & 1U) !=
         RVRT_FRAME_WORK_KIND_DATA)) {
        return RVRT_CODEC_STATUS_OK;
    }

    uint32_t timestep = 0U;
    uint32_t axon_bit_idx = 0U;
    if (rvrt_output_frame_address(&runner->output_view, frame, &timestep,
                                  &axon_bit_idx) != RVRT_CODEC_STATUS_OK) {
        return RVRT_CODEC_STATUS_OK;
    }
    if (timestep >= submitted_timesteps) {
        RV_DEBUG_LOGE(
            "paicore_runner",
            "unexpected DATA frame=0x%08x%08x timestep=%u axon=%u submitted=%u",
            (unsigned)frame->high, (unsigned)frame->low, (unsigned)timestep,
            (unsigned)axon_bit_idx, (unsigned)submitted_timesteps);
        return RVRT_CODEC_STATUS_OUT_OF_RANGE;
    }
    if ((timestep >= runner->runtime.timesteps) ||
        (axon_bit_idx < runner->fast_output_base)) {
        return RVRT_CODEC_STATUS_OK;
    }
    const uint32_t element = axon_bit_idx - runner->fast_output_base;
    if ((element >= runner->output_view.element_count) ||
        ((size_t)timestep > (SIZE_MAX - element) / output_stride)) {
        return RVRT_CODEC_STATUS_OK;
    }

    const uint32_t payload = frame->low & 0xFFU;
    if ((payload & ~1U) != 0U) {
        return RVRT_CODEC_STATUS_OK;
    }
    const size_t output_index = (size_t)timestep * output_stride + element;
    if (output_index >= output_capacity) {
        return RVRT_CODEC_STATUS_OUT_OF_RANGE;
    }
    output[output_index] = (uint8_t)payload;
    return RVRT_CODEC_STATUS_OK;
}

static inline rvrt_codec_status_t
runner_decode_voltage_fast_frame(const rvrt_paicore_runner_t *runner,
                                 const rvrt_frame_t *frame, uint8_t *output,
                                 size_t output_capacity, size_t output_stride,
                                 uint32_t submitted_timesteps)
{
    uint32_t timestep = 0U;
    uint32_t axon_bit_idx = 0U;
    if (!rvrt_frame_is_work(frame) ||
        (((frame->high >> RVRT_FRAME_WORK_KIND_OFFSET) & 1U) !=
         RVRT_FRAME_WORK_KIND_VOLTAGE) ||
        (rvrt_output_frame_address(&runner->output_view, frame, &timestep,
                                   &axon_bit_idx) != RVRT_CODEC_STATUS_OK)) {
        return RVRT_CODEC_STATUS_OK;
    }
    if (timestep >= submitted_timesteps) {
        return RVRT_CODEC_STATUS_OUT_OF_RANGE;
    }
    if (timestep >= runner->runtime.timesteps) {
        return RVRT_CODEC_STATUS_OK;
    }

    if (axon_bit_idx < runner->fast_output_base) {
        return RVRT_CODEC_STATUS_OK;
    }
    const uint32_t relative_axon = axon_bit_idx - runner->fast_output_base;
    const uint32_t group = relative_axon >> RUN_VOLT_GROUP_PITCH_SHIFT;
    const uint32_t within_group = relative_axon & (RUN_VOLT_GROUP_PITCH - 1U);
    const uint32_t element = (group << RUN_VOLT_GROUP_SIZE_SHIFT) |
                             (within_group & (RUN_VOLT_LANE_STRIDE - 1U));
    const uint32_t lane = within_group >> RUN_VOLT_LANE_ADDR_SHIFT;
    if ((lane >= RVRT_VOLT_LANE_COUNT) ||
        (element >= runner->output_view.element_count)) {
        return RVRT_CODEC_STATUS_OK;
    }
    const size_t output_index =
        (size_t)timestep * (output_stride / sizeof(int32_t)) + element;
    const size_t state_index =
        (size_t)timestep * runner->output_view.element_count + element;
    return rvrt_store_voltage_lane(
        (int32_t *)(void *)output, output_index,
        output_capacity / sizeof(int32_t), runner->voltage_state, state_index,
        runner->voltage_state_capacity, lane,
        (uint8_t)(frame->low & RVRT_WF_PAYLOAD_MASK), NULL);
}

static rvrt_session_status_t
runner_handle_codec_result(runner_decode_context_t *decode,
                           rvrt_codec_status_t status)
{
    if (status == RVRT_CODEC_STATUS_OK) {
        return RVRT_SESSION_OK;
    }
#if !RV_DEBUG_ENABLE_LOGGING
    (void)decode;
#endif
    RV_DEBUG_LOGE("paicore_runner", "decode completion_target=%u failed: %s",
                  (unsigned)decode->runner->runtime.completion_sync_timestep,
                  rvrt_codec_status_string(status));
    return RVRT_SESSION_RUNTIME_ERROR;
}

static rvrt_session_status_t
runner_handle_generic_frame(void *user_data, const rvrt_frame_t *frame)
{
    runner_decode_context_t *const decode = user_data;
    if ((decode == NULL) || (decode->runner == NULL) || (frame == NULL)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    const rvrt_session_status_t timestamp_status =
        runner_validate_frame_timestep(decode, frame);
    if (timestamp_status != RVRT_SESSION_OK) {
        return timestamp_status;
    }
    return runner_handle_codec_result(
        decode, rvrt_decode_output_frames_incremental(
                    &decode->runner->output_view, frame, 1U,
                    decode->runner->runtime.timesteps, decode->output,
                    decode->output_capacity, decode->output_stride,
                    decode->runner->voltage_state,
                    decode->runner->voltage_state_capacity));
}

static rvrt_session_status_t
runner_handle_voltage_fast_frame(void *user_data, const rvrt_frame_t *frame)
{
    runner_decode_context_t *const decode = user_data;
    if ((decode == NULL) || (decode->runner == NULL) || (frame == NULL)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    return runner_handle_codec_result(
        decode,
        runner_decode_voltage_fast_frame(
            decode->runner, frame, decode->output, decode->output_capacity,
            decode->output_stride, decode->submitted_timesteps));
}

static rvrt_session_status_t
runner_handle_data_fast_frame(void *user_data, const rvrt_frame_t *frame)
{
    runner_decode_context_t *const decode = user_data;
    if ((decode == NULL) || (decode->runner == NULL) || (frame == NULL)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    return runner_handle_codec_result(
        decode,
        runner_decode_data_fast_frame(
            decode->runner, frame, decode->output, decode->output_capacity,
            decode->output_stride, decode->submitted_timesteps));
}

static rvrt_session_rx_frame_handler_t
runner_rx_frame_handler(const rvrt_paicore_runner_t *runner)
{
    if (runner->has_fast_data_layout) {
        return runner_handle_data_fast_frame;
    }
    return runner->has_fast_voltage_layout ? runner_handle_voltage_fast_frame
                                           : runner_handle_generic_frame;
}

static rvrt_session_status_t
runner_mark_exact_slot(runner_decode_context_t *decode, uint32_t timestep,
                       uint32_t element, uint32_t lane, uint32_t lanes)
{
    const uint32_t per_timestep =
        decode->runner->output_view.element_count * lanes;
    if ((timestep >= decode->submitted_timesteps) ||
        (element >= decode->runner->output_view.element_count) ||
        (lane >= lanes)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    const uint32_t slot = timestep * per_timestep + element * lanes + lane;
    const uint32_t word = slot >> 5U;
    const uint32_t mask = UINT32_C(1) << (slot & 31U);
    if ((word >= decode->runner->coverage_word_capacity) ||
        ((decode->runner->coverage_bitmap[word] & mask) != 0U)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    decode->runner->coverage_bitmap[word] |= mask;
    decode->covered_slots++;
    return RVRT_SESSION_OK;
}

static rvrt_session_status_t
runner_handle_exact_frame(void *user_data, const rvrt_frame_t *frame,
                          bool *barrier_complete)
{
    runner_decode_context_t *const decode = user_data;
    if ((decode == NULL) || (decode->runner == NULL) || (frame == NULL) ||
        (barrier_complete == NULL)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    *barrier_complete = false;
    if (rvrt_frame_is_complete(frame)) {
        if (((frame->low & 0xFFFFFFU) != decode->runner->expected_thread_id) ||
            decode->complete_seen) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        decode->complete_seen = true;
    } else {
        if (!rvrt_frame_is_work(frame)) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        const uint32_t frame_kind =
            (frame->high >> RVRT_FRAME_WORK_KIND_OFFSET) & 1U;
        uint32_t timestep = 0U;
        uint32_t axon_bit_idx = 0U;
        if (rvrt_output_frame_address(&decode->runner->output_view, frame,
                                      &timestep,
                                      &axon_bit_idx) != RVRT_CODEC_STATUS_OK) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        rvrt_codec_status_t codec_status = RVRT_CODEC_STATUS_OK;
        rvrt_session_status_t coverage_status = RVRT_SESSION_OK;
        if (decode->runner->output_view.kind == RVRT_OUTPUT_DATA) {
            if ((frame_kind != RVRT_FRAME_WORK_KIND_DATA) ||
                ((frame->low & RVRT_WF_PAYLOAD_MASK) > 1U)) {
                return RVRT_SESSION_RUNTIME_ERROR;
            }
            uint32_t element = axon_bit_idx;
            if (decode->runner->has_fast_data_layout) {
                if (element < decode->runner->fast_output_base) {
                    return RVRT_SESSION_RUNTIME_ERROR;
                }
                element -= decode->runner->fast_output_base;
                if (element >= decode->runner->output_view.element_count) {
                    return RVRT_SESSION_RUNTIME_ERROR;
                }
                codec_status = runner_decode_data_fast_frame(
                    decode->runner, frame, decode->output,
                    decode->output_capacity, decode->output_stride,
                    decode->submitted_timesteps);
            } else {
                rvrt_artifact_output_entry_t entry = {0};
                bool found = false;
                if ((rvrt_artifact_output_mapping_find(
                         &decode->runner->output_view, axon_bit_idx, &entry,
                         &found) != RVRT_ARTIFACT_OK) ||
                    !found ||
                    (entry.elem_idx >=
                     decode->runner->output_view.element_count)) {
                    return RVRT_SESSION_RUNTIME_ERROR;
                }
                element = entry.elem_idx;
                codec_status = rvrt_decode_output_frames_incremental(
                    &decode->runner->output_view, frame, 1U,
                    decode->runner->runtime.timesteps, decode->output,
                    decode->output_capacity, decode->output_stride,
                    decode->runner->voltage_state,
                    decode->runner->voltage_state_capacity);
            }
            if (codec_status == RVRT_CODEC_STATUS_OK) {
                coverage_status =
                    runner_mark_exact_slot(decode, timestep, element, 0U, 1U);
            }
        } else if (decode->runner->output_view.kind == RVRT_OUTPUT_VOLTAGE) {
            const uint32_t lane = (axon_bit_idx >> RUN_VOLT_LANE_ADDR_SHIFT) &
                                  (RVRT_VOLT_LANE_COUNT - 1U);
            if (frame_kind != RVRT_FRAME_WORK_KIND_VOLTAGE) {
                return RVRT_SESSION_RUNTIME_ERROR;
            }
            uint32_t element = 0U;
            if (decode->runner->has_fast_voltage_layout) {
                if (axon_bit_idx < decode->runner->fast_output_base) {
                    return RVRT_SESSION_RUNTIME_ERROR;
                }
                const uint32_t relative_axon =
                    axon_bit_idx - decode->runner->fast_output_base;
                const uint32_t group =
                    relative_axon >> RUN_VOLT_GROUP_PITCH_SHIFT;
                const uint32_t within_group =
                    relative_axon & (RUN_VOLT_GROUP_PITCH - 1U);
                element = (group << RUN_VOLT_GROUP_SIZE_SHIFT) |
                          (within_group & (RUN_VOLT_GROUP_SIZE - 1U));
                if ((element >= decode->runner->output_view.element_count) ||
                    (lane >= RVRT_VOLT_LANE_COUNT)) {
                    return RVRT_SESSION_RUNTIME_ERROR;
                }
                codec_status = runner_decode_voltage_fast_frame(
                    decode->runner, frame, decode->output,
                    decode->output_capacity, decode->output_stride,
                    decode->submitted_timesteps);
            } else {
                const uint32_t base =
                    axon_bit_idx - lane * RUN_VOLT_LANE_STRIDE;
                rvrt_artifact_output_entry_t entry = {0};
                bool found = false;
                if ((rvrt_artifact_output_mapping_find(
                         &decode->runner->output_view, base, &entry, &found) !=
                     RVRT_ARTIFACT_OK) ||
                    !found ||
                    (entry.elem_idx >=
                     decode->runner->output_view.element_count)) {
                    return RVRT_SESSION_RUNTIME_ERROR;
                }
                element = entry.elem_idx;
                codec_status = rvrt_decode_output_frames_incremental(
                    &decode->runner->output_view, frame, 1U,
                    decode->runner->runtime.timesteps, decode->output,
                    decode->output_capacity, decode->output_stride,
                    decode->runner->voltage_state,
                    decode->runner->voltage_state_capacity);
            }
            if (codec_status == RVRT_CODEC_STATUS_OK) {
                coverage_status = runner_mark_exact_slot(
                    decode, timestep, element, lane, RVRT_VOLT_LANE_COUNT);
            }
        } else {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        if ((codec_status != RVRT_CODEC_STATUS_OK) ||
            (coverage_status != RVRT_SESSION_OK)) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
    }

    const uint32_t lanes =
        decode->runner->output_view.kind == RVRT_OUTPUT_VOLTAGE
            ? RVRT_VOLT_LANE_COUNT
            : 1U;
    const uint32_t expected = decode->submitted_timesteps *
                              decode->runner->output_view.element_count * lanes;
    if (decode->covered_slots > expected) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    *barrier_complete =
        decode->complete_seen && (decode->covered_slots == expected);
    return RVRT_SESSION_OK;
}

static rvrt_session_status_t runner_handle_exact_init(void *user_data,
                                                      const rvrt_frame_t *frame,
                                                      bool *barrier_complete)
{
    rvrt_paicore_runner_t *const runner = user_data;
    if ((runner == NULL) || (frame == NULL) || (barrier_complete == NULL) ||
        !rvrt_frame_is_complete(frame) ||
        ((frame->low & 0xFFFFFFU) != runner->expected_thread_id)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    *barrier_complete = true;
    return RVRT_SESSION_OK;
}

static bool sample_region_fits(size_t total_size, size_t row_size,
                               size_t stride, uint32_t timesteps)
{
    if ((row_size == 0U) || (stride < row_size) || (timesteps == 0U)) {
        return false;
    }
    const size_t rows_before_last = (size_t)timesteps - 1U;
    return (rows_before_last <= (SIZE_MAX - row_size) / stride) &&
           (rows_before_last * stride + row_size <= total_size);
}

static bool sample_regions_overlap(const void *left, size_t left_size,
                                   const void *right, size_t right_size)
{
    const uintptr_t left_begin = (uintptr_t)left;
    const uintptr_t right_begin = (uintptr_t)right;
    if ((left_size == 0U) || (right_size == 0U) ||
        (left_begin > UINTPTR_MAX - left_size) ||
        (right_begin > UINTPTR_MAX - right_size)) {
        return (left_size != 0U) && (right_size != 0U);
    }
    return (left_begin < right_begin + right_size) &&
           (right_begin < left_begin + left_size);
}

static bool input_route_component_valid(int32_t value)
{
    return (value >= -31) && (value <= 31);
}

static bool same_input_cover(const rvrt_artifact_input_entry_t *left,
                             const rvrt_artifact_input_entry_t *right)
{
    return (left->core_offset.xy == right->core_offset.xy) &&
           (left->core_offset.x == right->core_offset.x) &&
           (left->core_offset.y == right->core_offset.y) &&
           (left->copy_count.xy == right->copy_count.xy) &&
           (left->copy_count.x == right->copy_count.x) &&
           (left->copy_count.y == right->copy_count.y) &&
           (left->target_lcn == right->target_lcn) &&
           (left->copy_id == right->copy_id) && (left->dtype == right->dtype);
}

static rvrt_session_status_t
validate_input_schedule(rvrt_paicore_runner_t *runner, uint32_t total_timesteps)
{
    const rvrt_artifact_input_mapping_view_t *const view = &runner->input_view;
    if ((view == NULL) || (view->entries == NULL) ||
        (view->entry_count == 0U)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    runner->has_fast_input_layout = false;
    runner->fast_input_cover_count = 0U;
    bool canonical = (view->bit_width == 8U) && (view->element_count != 0U) &&
                     (view->entry_count % view->element_count == 0U);
    const uint32_t cover_count =
        canonical ? view->entry_count / view->element_count : 0U;
    canonical = canonical && (cover_count != 0U) &&
                (cover_count <= RVRT_PAICORE_RUNNER_INPUT_COVER_MAX);

    for (uint32_t index = 0U; index < view->entry_count; ++index) {
        rvrt_artifact_input_entry_t entry = {0};
        if (rvrt_artifact_input_mapping_entry(view, index, &entry) !=
            RVRT_ARTIFACT_OK) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        if (entry.target_lcn >= 8U) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        const uint32_t tick_relative_capacity = 1U << entry.target_lcn;
        if (entry.tick_relative >= tick_relative_capacity) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        const uint32_t timestamp_capacity = 1U << (8U - entry.target_lcn);
        if (total_timesteps > timestamp_capacity) {
            return RVRT_SESSION_SCHEDULE_UNSUPPORTED;
        }

        if (canonical) {
            const uint32_t element = index / cover_count;
            const uint32_t cover = index % cover_count;
            const bool route_valid =
                input_route_component_valid(entry.core_offset.xy) &&
                input_route_component_valid(entry.core_offset.x) &&
                input_route_component_valid(entry.core_offset.y) &&
                input_route_component_valid(entry.copy_count.xy) &&
                input_route_component_valid(entry.copy_count.x) &&
                input_route_component_valid(entry.copy_count.y);
            const bool entry_canonical =
                route_valid && (entry.elem_idx == element) &&
                (entry.tick_relative == element / 64U) &&
                (entry.addr_axon == (element % 64U) * 8U) &&
                (entry.addr_axon <= RVRT_WF_AX_MASK) && (entry.copy_id == 0U) &&
                (entry.dtype == RUN_INPUT_INT8) &&
                ((cover == 0U) ||
                 (entry.target_lcn ==
                  runner->fast_input_prototypes[0].target_lcn)) &&
                ((element == 0U) ||
                 same_input_cover(&runner->fast_input_prototypes[cover],
                                  &entry));
            if (!entry_canonical) {
                canonical = false;
            } else if (element == 0U) {
                runner->fast_input_prototypes[cover] = entry;
            }
        }
    }
    if (canonical && (view->element_count <= UINT32_MAX / cover_count) &&
        (view->element_count * cover_count == view->entry_count)) {
        runner->fast_input_cover_count = cover_count;
        runner->has_fast_input_layout = true;
    }
    return RVRT_SESSION_OK;
}

static bool voltage_state_capacity(uint32_t timesteps, uint32_t element_count,
                                   uint32_t *required_out)
{
    if ((timesteps == 0U) || (element_count == 0U) ||
        (timesteps > UINT32_MAX / element_count) || (required_out == NULL)) {
        return false;
    }
    *required_out = timesteps * element_count;
    return true;
}

rvrt_session_status_t
rvrt_paicore_runner_prepare(rvrt_paicore_runner_t *runner,
                            const rvrt_paicore_runner_prepare_config_t *config)
{
    if ((runner == NULL) || (config == NULL)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (runner->state != RVRT_PAICORE_RUNNER_EMPTY) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    memset(runner, 0, sizeof(*runner));

    rvrt_session_status_t failure_status = RVRT_SESSION_RUNTIME_ERROR;

    if ((config->artifact_data == NULL) || (config->timeout_ms == 0U) ||
        ((config->rx_policy != RVRT_PAICORE_RUNNER_RX_SPARSE) &&
         (config->rx_policy != RVRT_PAICORE_RUNNER_RX_EXACT))) {
        goto runtime_error;
    }
    if (rvrt_artifact_read(config->artifact_data, config->artifact_size,
                           &runner->artifact) != RVRT_ARTIFACT_OK) {
        goto runtime_error;
    }

    if ((rvrt_artifact_thread_id(&runner->artifact, 0U,
                                 &runner->expected_thread_id) !=
         RVRT_ARTIFACT_OK) ||
        (runner->expected_thread_id > UINT32_C(0xFFFFFF)) ||
        (rvrt_artifact_thread_runtime(&runner->artifact, 0U,
                                      &runner->runtime) != RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_input_mapping_view(&runner->artifact, 0U, 0U,
                                              &runner->input_view) !=
         RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_output_mapping_view(&runner->artifact, 0U, 0U,
                                               &runner->output_view) !=
         RVRT_ARTIFACT_OK) ||
        (runner->runtime.timesteps == 0U) ||
        (runner->runtime.pipeline_latency == 0U) ||
        ((config->rx_policy == RVRT_PAICORE_RUNNER_RX_EXACT) &&
         (runner->runtime.pipeline_latency != 1U)) ||
        (runner->runtime.output_time_encoding !=
         RVRT_OUTPUT_TIME_ENCODING_STREAM) ||
        (runner->runtime.timesteps >
         UINT32_MAX - runner->runtime.pipeline_latency + 1U) ||
        (runner->runtime.completion_sync_timestep !=
         runner->runtime.pipeline_latency + runner->runtime.timesteps - 1U) ||
        (runner->runtime.completion_sync_timestep > 0xFFFFFFU) ||
        (runner->input_view.element_count == 0U) ||
        (runner->output_view.element_count == 0U) ||
        (runner->output_view.target_lcn > 7U)) {
        goto runtime_error;
    }
    runner->input_row_bytes = runner->input_view.element_count;
    if (runner->output_view.kind == RVRT_OUTPUT_DATA) {
        runner->output_row_bytes = runner->output_view.element_count;
    } else if (runner->output_view.kind == RVRT_OUTPUT_VOLTAGE) {
        if (runner->output_view.element_count > SIZE_MAX / sizeof(int32_t)) {
            goto runtime_error;
        }
        runner->output_row_bytes =
            (size_t)runner->output_view.element_count * sizeof(int32_t);
    } else {
        goto runtime_error;
    }

    const rvrt_session_status_t input_window_status =
        validate_input_schedule(runner, runner->runtime.timesteps);
    if (input_window_status != RVRT_SESSION_OK) {
        RV_DEBUG_LOGE("paicore_runner", "input window T=%u cannot fit target",
                      (unsigned)runner->runtime.timesteps);
        failure_status = input_window_status;
        goto runtime_error;
    }

    uint32_t required_voltage_state = 0U;
    if (runner->output_view.kind == RVRT_OUTPUT_VOLTAGE) {
        if (!voltage_state_capacity(runner->runtime.timesteps,
                                    runner->output_view.element_count,
                                    &required_voltage_state) ||
            (config->voltage_state == NULL) ||
            (config->voltage_state_capacity < required_voltage_state)) {
            failure_status = RVRT_SESSION_BUFFER_TOO_SMALL;
            goto runtime_error;
        }
    }

    if (configure_fast_layout(runner) != RVRT_SESSION_OK) {
        goto runtime_error;
    }
    if (config->rx_policy == RVRT_PAICORE_RUNNER_RX_EXACT) {
        const bool exact_data =
            (runner->output_view.kind == RVRT_OUTPUT_DATA) &&
            (runner->output_view.dtype == RUN_DATA_UINT1);
        const bool exact_voltage =
            (runner->output_view.kind == RVRT_OUTPUT_VOLTAGE) &&
            (runner->output_view.dtype == RVRT_DTYPE_VOLTAGE_INT32);
        if ((!exact_data && !exact_voltage) ||
            (runner->output_view.entry_count !=
             runner->output_view.element_count)) {
            failure_status = RVRT_SESSION_SCHEDULE_UNSUPPORTED;
            goto runtime_error;
        }
    }

    runner->voltage_state = config->voltage_state;
    runner->voltage_state_capacity = config->voltage_state_capacity;
    runner->timeout_ms = config->timeout_ms;
    runner->rx_policy = config->rx_policy;
    const uint32_t lanes = runner->output_view.kind == RVRT_OUTPUT_VOLTAGE
                               ? RVRT_VOLT_LANE_COUNT
                               : 1U;
    if ((runner->runtime.timesteps >
         UINT32_MAX / runner->output_view.element_count) ||
        (runner->runtime.timesteps * runner->output_view.element_count >
         UINT32_MAX / lanes)) {
        goto runtime_error;
    }
    runner->exact_slot_count =
        runner->runtime.timesteps * runner->output_view.element_count * lanes;
    runner->state = RVRT_PAICORE_RUNNER_PREPARED;
    return RVRT_SESSION_OK;

runtime_error:
    memset(runner, 0, sizeof(*runner));
    return failure_status;
}

rvrt_session_status_t
rvrt_paicore_runner_attach(rvrt_paicore_runner_t *runner,
                           const rvrt_paicore_runner_attach_config_t *config)
{
    if ((runner == NULL) || (config == NULL) ||
        (runner->state != RVRT_PAICORE_RUNNER_PREPARED) ||
        runner->config_load_failed || (config->frame_buffer == NULL) ||
        (config->frame_capacity == 0U)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (runner->rx_policy == RVRT_PAICORE_RUNNER_RX_EXACT) {
        const uint32_t required_words = (runner->exact_slot_count + 31U) / 32U;
        if ((config->coverage_bitmap == NULL) ||
            (config->coverage_word_capacity < required_words)) {
            return RVRT_SESSION_BUFFER_TOO_SMALL;
        }
    }
    const rvrt_session_config_t session_config = {
        .artifact = &runner->artifact,
        .thread_index = 0U,
        .rx_frames = config->frame_buffer,
        .rx_capacity = config->frame_capacity,
    };
    const rvrt_session_status_t status =
        rvrt_session_init(&runner->session, &session_config);
    if (status != RVRT_SESSION_OK) {
        return status;
    }
    runner->coverage_bitmap = config->coverage_bitmap;
    runner->coverage_word_capacity = config->coverage_word_capacity;
    runner->encode_frame_capacity = config->frame_capacity;
    runner->state = RVRT_PAICORE_RUNNER_ATTACHED;
    return RVRT_SESSION_OK;
}

rvrt_session_status_t
rvrt_paicore_runner_load_config(rvrt_paicore_runner_t *runner)
{
    if ((runner == NULL) || (runner->state != RVRT_PAICORE_RUNNER_ATTACHED) ||
        runner->config_loaded || runner->config_load_failed) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    const rvrt_session_status_t status =
        rvrt_session_load_config(&runner->session);
    if (status == RVRT_SESSION_OK) {
        runner->config_loaded = true;
    } else {
        runner->config_load_failed = true;
    }
    return status;
}

rvrt_session_status_t rvrt_paicore_runner_detach(rvrt_paicore_runner_t *runner)
{
    if ((runner == NULL) || (runner->state != RVRT_PAICORE_RUNNER_ATTACHED)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    const rvrt_session_status_t status = rvrt_session_deinit(&runner->session);
    if (status == RVRT_SESSION_OK) {
        runner->coverage_bitmap = NULL;
        runner->coverage_word_capacity = 0U;
        runner->encode_frame_capacity = 0U;
        runner->state = RVRT_PAICORE_RUNNER_PREPARED;
    }
    return status;
}

rvrt_session_status_t
rvrt_paicore_runner_deploy(rvrt_paicore_runner_t *runner,
                           const rvrt_paicore_runner_deploy_config_t *config)
{
    if ((runner == NULL) || (config == NULL)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    const rvrt_session_status_t release_status =
        rvrt_paicore_runner_release(runner);
    if (release_status != RVRT_SESSION_OK) {
        return release_status;
    }
    const rvrt_paicore_runner_prepare_config_t prepare_config = {
        .artifact_data = config->artifact_data,
        .artifact_size = config->artifact_size,
        .voltage_state = config->voltage_state,
        .voltage_state_capacity = config->voltage_state_capacity,
        .timeout_ms = config->timeout_ms,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_SPARSE,
    };
    rvrt_session_status_t status =
        rvrt_paicore_runner_prepare(runner, &prepare_config);
    if (status != RVRT_SESSION_OK) {
        return status;
    }
    const rvrt_paicore_runner_attach_config_t attach_config = {
        .frame_buffer = config->frame_buffer,
        .frame_capacity = config->frame_capacity,
    };
    status = rvrt_paicore_runner_attach(runner, &attach_config);
    if (status == RVRT_SESSION_OK) {
        status = rvrt_paicore_runner_load_config(runner);
    }
    if (status != RVRT_SESSION_OK) {
        (void)rvrt_paicore_runner_release(runner);
    }
    return status;
}

rvrt_session_status_t rvrt_paicore_runner_release(rvrt_paicore_runner_t *runner)
{
    if (runner == NULL) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    rvrt_session_status_t status = RVRT_SESSION_OK;
    if (runner->state == RVRT_PAICORE_RUNNER_ATTACHED) {
        status = rvrt_paicore_runner_detach(runner);
    } else if (runner->state != RVRT_PAICORE_RUNNER_PREPARED &&
               runner->state != RVRT_PAICORE_RUNNER_EMPTY) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (status == RVRT_SESSION_OK) {
        memset(runner, 0, sizeof(*runner));
    }
    return status;
}

rvrt_session_status_t
rvrt_paicore_runner_get_stats(const rvrt_paicore_runner_t *runner,
                              rvrt_session_stats_t *stats)
{
    if ((runner == NULL) || (runner->state != RVRT_PAICORE_RUNNER_ATTACHED) ||
        (runner->session.artifact != &runner->artifact)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    return rvrt_session_get_stats(&runner->session, stats);
}

rvrt_session_status_t rvrt_paicore_runner_exchange_exact(
    rvrt_paicore_runner_t *runner, const rvrt_frame_t *request,
    uint32_t rx_goal, uint32_t timeout_ms,
    rvrt_session_rx_exact_frame_handler_t handler, void *user_data)
{
    if ((runner == NULL) || (runner->state != RVRT_PAICORE_RUNNER_ATTACHED) ||
        !runner->config_loaded || runner->config_load_failed ||
        (runner->session.artifact != &runner->artifact)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    return rvrt_session_exchange_exact(&runner->session, request, rx_goal,
                                       timeout_ms, handler, user_data);
}

static rvrt_session_status_t
runner_exact_step_rx_goal(const rvrt_paicore_runner_t *runner,
                          uint32_t *rx_goal)
{
    if ((runner == NULL) || (rx_goal == NULL) ||
        (runner->runtime.timesteps == 0U) ||
        ((runner->exact_slot_count % runner->runtime.timesteps) != 0U)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    const uint32_t output_frames =
        runner->exact_slot_count / runner->runtime.timesteps;
    if (output_frames == UINT32_MAX) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    *rx_goal = output_frames + 1U;
    return RVRT_SESSION_OK;
}

rvrt_session_status_t rvrt_paicore_runner_run_sample_profiled(
    rvrt_paicore_runner_t *runner, const uint8_t *input, size_t input_capacity,
    size_t input_stride, void *output, size_t output_capacity,
    size_t output_stride, rvrt_paicore_runner_sample_timing_t *timing)
{
    if ((runner == NULL) || (runner->state != RVRT_PAICORE_RUNNER_ATTACHED) ||
        !runner->config_loaded ||
        (runner->session.artifact != &runner->artifact) ||
        (runner->input_view.entries == NULL) ||
        (runner->output_view.entries == NULL) || (input == NULL) ||
        (output == NULL) || (runner->encode_frame_capacity == 0U)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
#if RVRT_ENABLE_STATS
    if (timing != NULL) {
        const uint32_t sync_count = runner->runtime.timesteps +
                                    (runner->runtime.completion_sync_timestep >
                                             runner->runtime.timesteps
                                         ? 1U
                                         : 0U);
        if ((timing->sync_round_trip_cycles == NULL) ||
            (timing->sync_round_trip_capacity < sync_count)) {
            return RVRT_SESSION_BUFFER_TOO_SMALL;
        }
        timing->init_round_trip_cycles = 0U;
        timing->sync_round_trip_count = 0U;
    }
#else
    (void)timing;
#endif
    const size_t effective_input_stride =
        (input_stride == 0U) ? runner->input_row_bytes : input_stride;
    const size_t effective_output_stride =
        (output_stride == 0U) ? runner->output_row_bytes : output_stride;
    uint8_t *const output_bytes = output;
    if (!sample_region_fits(input_capacity, runner->input_row_bytes,
                            effective_input_stride,
                            runner->runtime.timesteps) ||
        !sample_region_fits(output_capacity, runner->output_row_bytes,
                            effective_output_stride,
                            runner->runtime.timesteps)) {
        return RVRT_SESSION_BUFFER_TOO_SMALL;
    }
    const size_t input_region_size =
        (size_t)(runner->runtime.timesteps - 1U) * effective_input_stride +
        runner->input_row_bytes;
    const size_t output_region_size =
        (size_t)(runner->runtime.timesteps - 1U) * effective_output_stride +
        runner->output_row_bytes;
    if (sample_regions_overlap(input, input_region_size, output_bytes,
                               output_region_size) &&
        ((input != output_bytes) ||
         (effective_input_stride != effective_output_stride))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    uint32_t required_voltage_state = 0U;
    if (runner->output_view.kind == RVRT_OUTPUT_VOLTAGE) {
        if ((((uintptr_t)output_bytes % sizeof(int32_t)) != 0U) ||
            ((output_capacity % sizeof(int32_t)) != 0U) ||
            ((effective_output_stride % sizeof(int32_t)) != 0U) ||
            !voltage_state_capacity(runner->runtime.timesteps,
                                    runner->output_view.element_count,
                                    &required_voltage_state) ||
            (runner->voltage_state == NULL) ||
            (runner->voltage_state_capacity < required_voltage_state)) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        memset(runner->voltage_state, 0,
               (size_t)required_voltage_state * sizeof(*runner->voltage_state));
    }
    if (runner->rx_policy == RVRT_PAICORE_RUNNER_RX_EXACT) {
        const uint32_t coverage_words = (runner->exact_slot_count + 31U) / 32U;
        memset(runner->coverage_bitmap, 0,
               (size_t)coverage_words * sizeof(*runner->coverage_bitmap));
    }

    uint32_t exact_step_rx_goal = 0U;
    if ((runner->rx_policy == RVRT_PAICORE_RUNNER_RX_EXACT) &&
        (runner_exact_step_rx_goal(runner, &exact_step_rx_goal) !=
         RVRT_SESSION_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    RV_DEBUG_LOGI("paicore_runner", "sample reset begin");
#if RVRT_ENABLE_STATS
    const rv_counter_t init_round_trip_start = __get_rv_cycle();
#endif
    rvrt_session_status_t status =
        runner->rx_policy == RVRT_PAICORE_RUNNER_RX_EXACT
            ? rvrt_session_reset_model_with_exact_rx_handler(
                  &runner->session, 1U, runner->timeout_ms,
                  runner_handle_exact_init, runner)
            : rvrt_session_reset_model(&runner->session, runner->timeout_ms);
#if RVRT_ENABLE_STATS
    if (timing != NULL) {
        timing->init_round_trip_cycles =
            __get_rv_cycle() - init_round_trip_start;
    }
#endif
    if (status != RVRT_SESSION_OK) {
        return runner_session_failure("reset", 0U, status);
    }
    RV_DEBUG_LOGI("paicore_runner", "sample reset complete");

    runner_decode_context_t decode = {
        .runner = runner,
        .output = output_bytes,
        .output_capacity = output_capacity,
        .output_stride = effective_output_stride,
        .submitted_timesteps = 0U,
        .covered_slots = 0U,
        .complete_seen = false,
    };
    const rvrt_session_rx_frame_handler_t rx_frame_handler =
        runner_rx_frame_handler(runner);

    for (uint32_t timestep = 0U; timestep < runner->runtime.timesteps;
         ++timestep) {
        const uint8_t *const input_row =
            input + (size_t)timestep * effective_input_stride;
        RV_DEBUG_LOGI("paicore_runner", "timestep=%u send begin",
                      (unsigned)timestep);
        status =
            runner->has_fast_input_layout
                ? rvrt_session_send_canonical_input_timestep(
                      &runner->session, &runner->input_view,
                      runner->fast_input_prototypes,
                      runner->fast_input_cover_count, timestep, input_row,
                      runner->input_row_bytes, runner->session.rx_frames,
                      runner->encode_frame_capacity)
                : rvrt_session_send_input_timestep(
                      &runner->session, &runner->input_view, timestep,
                      input_row, runner->input_row_bytes,
                      runner->session.rx_frames, runner->encode_frame_capacity);
        if (status != RVRT_SESSION_OK) {
            return runner_session_failure("send", timestep, status);
        }

        memset(output_bytes + (size_t)timestep * effective_output_stride, 0,
               runner->output_row_bytes);
        const uint32_t completed_timesteps = timestep + 1U;
        decode.submitted_timesteps = completed_timesteps;
        decode.complete_seen = false;
        __WMB();
        RV_DEBUG_LOGI("paicore_runner", "timestep=%u sync begin",
                      (unsigned)timestep);
#if RVRT_ENABLE_STATS
        const rv_counter_t sync_round_trip_start = __get_rv_cycle();
#endif
        status =
            runner->rx_policy == RVRT_PAICORE_RUNNER_RX_EXACT
                ? rvrt_session_sync_wait_until_with_exact_rx_handler(
                      &runner->session, completed_timesteps, exact_step_rx_goal,
                      runner->timeout_ms, runner_handle_exact_frame, &decode)
                : rvrt_session_sync_wait_until_with_rx_handler(
                      &runner->session, completed_timesteps, runner->timeout_ms,
                      rx_frame_handler, &decode);
#if RVRT_ENABLE_STATS
        if (timing != NULL) {
            timing->sync_round_trip_cycles[timing->sync_round_trip_count++] =
                __get_rv_cycle() - sync_round_trip_start;
        }
#endif
        if (status != RVRT_SESSION_OK) {
            return runner_session_failure("sync", completed_timesteps, status);
        }
        RV_DEBUG_LOGI("paicore_runner", "timestep=%u complete",
                      (unsigned)timestep);
    }

    if (runner->runtime.completion_sync_timestep > runner->runtime.timesteps) {
#if RVRT_ENABLE_STATS
        const rv_counter_t completion_round_trip_start = __get_rv_cycle();
#endif
        decode.complete_seen = false;
        status =
            runner->rx_policy == RVRT_PAICORE_RUNNER_RX_EXACT
                ? rvrt_session_sync_wait_until_with_exact_rx_handler(
                      &runner->session,
                      runner->runtime.completion_sync_timestep, 1U,
                      runner->timeout_ms, runner_handle_exact_frame, &decode)
                : rvrt_session_sync_wait_until_with_rx_handler(
                      &runner->session,
                      runner->runtime.completion_sync_timestep,
                      runner->timeout_ms, rx_frame_handler, &decode);
#if RVRT_ENABLE_STATS
        if (timing != NULL) {
            timing->sync_round_trip_cycles[timing->sync_round_trip_count++] =
                __get_rv_cycle() - completion_round_trip_start;
        }
#endif
        if (status != RVRT_SESSION_OK) {
            return runner_session_failure(
                "completion sync", runner->runtime.completion_sync_timestep,
                status);
        }
    }
    return RVRT_SESSION_OK;
}

rvrt_session_status_t
rvrt_paicore_runner_run_sample(rvrt_paicore_runner_t *runner,
                               const uint8_t *input, size_t input_capacity,
                               size_t input_stride, void *output,
                               size_t output_capacity, size_t output_stride)
{
    return rvrt_paicore_runner_run_sample_profiled(
        runner, input, input_capacity, input_stride, output, output_capacity,
        output_stride, NULL);
}
