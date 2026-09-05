#include "paicore_runner.h"

#include <string.h>

#include "debug.h"
#include "frame_codec_internal.h"
#include "session_internal.h"
#include "session_io.h"

#define RUNNER_VOLTAGE_LANE_ADDRESS_SHIFT 3U
#define RUNNER_VOLTAGE_LANE_STRIDE 8U
#define RUNNER_VOLTAGE_GROUP_SIZE 8U
#define RUNNER_VOLTAGE_GROUP_PITCH 32U
#define RUNNER_VOLTAGE_GROUP_SIZE_SHIFT 3U
#define RUNNER_VOLTAGE_GROUP_PITCH_SHIFT 5U
#define RUNNER_DATA_DTYPE_UINT1 1U

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
    const bool is_data = (runner->output_view.kind == RVRT_OUTPUT_DATA) &&
                         (runner->output_view.dtype == RUNNER_DATA_DTYPE_UINT1);
    const bool is_voltage =
        (runner->output_view.kind == RVRT_OUTPUT_VOLTAGE) &&
        (runner->output_view.dtype == RVRT_DTYPE_VOLTAGE_INT32);
    if ((!is_data && !is_voltage) ||
        (runner->output_view.entry_count !=
         runner->output_view.element_count) ||
        (runner->output_view.element_count == 0U)) {
        return RVRT_SESSION_OK;
    }

    for (uint32_t element = 0U; element < runner->output_view.element_count;
         ++element) {
        rvrt_artifact_output_entry_t entry = {0};
        bool found = false;
        const uint32_t expected_axon =
            is_voltage ? ((element >> RUNNER_VOLTAGE_LANE_ADDRESS_SHIFT)
                          << RUNNER_VOLTAGE_GROUP_PITCH_SHIFT) |
                             (element & (RUNNER_VOLTAGE_GROUP_SIZE - 1U))
                       : element;
        if ((rvrt_artifact_output_mapping_find(&runner->output_view,
                                               expected_axon, &entry,
                                               &found) != RVRT_ARTIFACT_OK) ||
            !found) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        if ((entry.elem_idx != element) ||
            (entry.axon_bit_idx != expected_axon)) {
            return RVRT_SESSION_OK;
        }
    }

    runner->has_fast_data_layout = is_data;
    runner->has_fast_voltage_layout = is_voltage;
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
        (axon_bit_idx >= runner->output_view.element_count) ||
        ((size_t)timestep > (SIZE_MAX - axon_bit_idx) / output_stride)) {
        return RVRT_CODEC_STATUS_OK;
    }

    const uint32_t payload = frame->low & 0xFFU;
    if ((payload & ~1U) != 0U) {
        return RVRT_CODEC_STATUS_OK;
    }
    const size_t output_index = (size_t)timestep * output_stride + axon_bit_idx;
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

    const uint32_t group = axon_bit_idx >> RUNNER_VOLTAGE_GROUP_PITCH_SHIFT;
    const uint32_t within_group =
        axon_bit_idx & (RUNNER_VOLTAGE_GROUP_PITCH - 1U);
    const uint32_t element = (group << RUNNER_VOLTAGE_GROUP_SIZE_SHIFT) |
                             (within_group & (RUNNER_VOLTAGE_LANE_STRIDE - 1U));
    const uint32_t lane = within_group >> RUNNER_VOLTAGE_LANE_ADDRESS_SHIFT;
    if ((lane >= RVRT_VOLTAGE_LANE_COUNT) ||
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
        (uint8_t)(frame->low & RVRT_WORK_FRAME_PAYLOAD_MASK), NULL);
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

static rvrt_session_status_t
validate_input_schedule(const rvrt_artifact_input_mapping_view_t *view,
                        uint32_t total_timesteps)
{
    if ((view == NULL) || (view->entries == NULL) ||
        (view->entry_count == 0U)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

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
rvrt_paicore_runner_deploy(rvrt_paicore_runner_t *runner,
                           const rvrt_paicore_runner_deploy_config_t *config)
{
    if ((runner == NULL) || (config == NULL)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    const rvrt_session_status_t release_status =
        rvrt_session_deinit(&runner->session);
    if (release_status != RVRT_SESSION_OK) {
        return release_status;
    }
    memset(runner, 0, sizeof(*runner));

    rvrt_session_status_t failure_status = RVRT_SESSION_RUNTIME_ERROR;
    bool session_initialized = false;

    if ((config->artifact_data == NULL) || (config->frame_buffer == NULL) ||
        (config->frame_capacity == 0U) || (config->timeout_ms == 0U)) {
        goto runtime_error;
    }
    if (rvrt_artifact_read(config->artifact_data, config->artifact_size,
                           &runner->artifact) != RVRT_ARTIFACT_OK) {
        goto runtime_error;
    }

    const rvrt_session_config_t session_config = {
        .artifact = &runner->artifact,
        .thread_index = 0U,
        .rx_frames = config->frame_buffer,
        .rx_capacity = config->frame_capacity,
    };
    failure_status = rvrt_session_init(&runner->session, &session_config);
    if (failure_status != RVRT_SESSION_OK) {
        goto runtime_error;
    }
    session_initialized = true;

    if ((rvrt_artifact_thread_runtime(&runner->artifact, 0U,
                                      &runner->runtime) != RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_input_mapping_view(&runner->artifact, 0U, 0U,
                                              &runner->input_view) !=
         RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_output_mapping_view(&runner->artifact, 0U, 0U,
                                               &runner->output_view) !=
         RVRT_ARTIFACT_OK) ||
        (runner->runtime.timesteps == 0U) ||
        (runner->runtime.pipeline_latency == 0U) ||
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
        validate_input_schedule(&runner->input_view, runner->runtime.timesteps);
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

    runner->voltage_state = config->voltage_state;
    runner->voltage_state_capacity = config->voltage_state_capacity;
    runner->timeout_ms = config->timeout_ms;
    runner->encode_frame_capacity = config->frame_capacity;

    const rvrt_session_status_t load_status =
        rvrt_session_load_config(&runner->session);
    if (load_status != RVRT_SESSION_OK) {
        RV_DEBUG_LOGE("paicore_runner", "load config failed: %s",
                      rvrt_session_status_string(load_status));
        failure_status = load_status;
        goto runtime_error;
    }
    return RVRT_SESSION_OK;

runtime_error:
    if (session_initialized) {
        (void)rvrt_session_deinit(&runner->session);
    }
    memset(runner, 0, sizeof(*runner));
    return failure_status;
}

rvrt_session_status_t rvrt_paicore_runner_release(rvrt_paicore_runner_t *runner)
{
    if (runner == NULL) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    const rvrt_session_status_t status = rvrt_session_deinit(&runner->session);
    if (status == RVRT_SESSION_OK) {
        memset(runner, 0, sizeof(*runner));
    }
    return status;
}

rvrt_session_status_t
rvrt_paicore_runner_get_stats(const rvrt_paicore_runner_t *runner,
                              rvrt_session_stats_t *stats)
{
    if ((runner == NULL) || (runner->session.artifact != &runner->artifact)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    return rvrt_session_get_stats(&runner->session, stats);
}

rvrt_session_status_t
rvrt_paicore_runner_run_sample(rvrt_paicore_runner_t *runner,
                               const uint8_t *input, size_t input_capacity,
                               size_t input_stride, void *output,
                               size_t output_capacity, size_t output_stride)
{
    if ((runner == NULL) || (runner->session.artifact != &runner->artifact) ||
        (runner->input_view.entries == NULL) ||
        (runner->output_view.entries == NULL) || (input == NULL) ||
        (output == NULL) || (runner->encode_frame_capacity == 0U)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
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

    RV_DEBUG_LOGI("paicore_runner", "sample reset begin");
    rvrt_session_status_t status =
        rvrt_session_reset_model(&runner->session, runner->timeout_ms);
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
    };
    const rvrt_session_rx_frame_handler_t rx_frame_handler =
        runner_rx_frame_handler(runner);

    for (uint32_t timestep = 0U; timestep < runner->runtime.timesteps;
         ++timestep) {
        const uint8_t *const input_row =
            input + (size_t)timestep * effective_input_stride;
        RV_DEBUG_LOGI("paicore_runner", "timestep=%u send begin",
                      (unsigned)timestep);
        status = rvrt_session_send_input_timestep(
            &runner->session, &runner->input_view, timestep, input_row,
            runner->input_row_bytes, runner->session.rx_frames,
            runner->encode_frame_capacity);
        if (status != RVRT_SESSION_OK) {
            return runner_session_failure("send", timestep, status);
        }

        memset(output_bytes + (size_t)timestep * effective_output_stride, 0,
               runner->output_row_bytes);
        const uint32_t completed_timesteps = timestep + 1U;
        decode.submitted_timesteps = completed_timesteps;
        __WMB();
        RV_DEBUG_LOGI("paicore_runner", "timestep=%u sync begin",
                      (unsigned)timestep);
        status = rvrt_session_sync_wait_until_with_rx_handler(
            &runner->session, completed_timesteps, runner->timeout_ms,
            rx_frame_handler, &decode);
        if (status != RVRT_SESSION_OK) {
            return runner_session_failure("sync", completed_timesteps, status);
        }
        RV_DEBUG_LOGI("paicore_runner", "timestep=%u complete",
                      (unsigned)timestep);
    }

    if (runner->runtime.completion_sync_timestep > runner->runtime.timesteps) {
        status = rvrt_session_sync_wait_until_with_rx_handler(
            &runner->session, runner->runtime.completion_sync_timestep,
            runner->timeout_ms, rx_frame_handler, &decode);
        if (status != RVRT_SESSION_OK) {
            return runner_session_failure(
                "completion sync", runner->runtime.completion_sync_timestep,
                status);
        }
    }
    return RVRT_SESSION_OK;
}
