#include "thread_runner.h"

#include <string.h>

#include "debug.h"
#include "frame_codec_internal.h"
#include "runtime_session_internal.h"
#include "transport_io_internal.h"
#include "transport_thread_internal.h"

typedef struct runner_decode_context_s {
    rvrt_thread_runner_t *runner;
    uint8_t *output;
    size_t output_capacity;
    size_t output_stride;
    uint32_t submitted_timesteps;
} runner_decode_context_t;

static rvrt_runtime_status_t
runner_validate_frame_timestep(const runner_decode_context_t *decode,
                               const rvrt_frame_t *frame)
{
    if (!rvrt_frame_is_work(frame)) {
        return RVRT_RUNTIME_OK;
    }
    uint32_t timestep = 0U;
    uint32_t axon_bit_idx = 0U;
    if (rvrt_output_frame_address(&decode->runner->output_view, frame,
                                  &timestep,
                                  &axon_bit_idx) != RVRT_CODEC_STATUS_OK) {
        return RVRT_RUNTIME_OK;
    }
    (void)axon_bit_idx;
    return (timestep < decode->submitted_timesteps)
               ? RVRT_RUNTIME_OK
               : RVRT_RUNTIME_RUNTIME_ERROR;
}

static rvrt_runtime_status_t
runner_handle_codec_result(runner_decode_context_t *decode,
                           rvrt_codec_status_t status)
{
    if (status == RVRT_CODEC_STATUS_OK) {
        return RVRT_RUNTIME_OK;
    }
#if !RV_DEBUG_ENABLE_LOGGING
    (void)decode;
#endif
    RV_DEBUG_LOGE("thread_runner", "decode completion_target=%u failed: %s",
                  (unsigned)decode->runner->runtime.completion_sync_timestep,
                  rvrt_codec_status_string(status));
    return RVRT_RUNTIME_RUNTIME_ERROR;
}

static rvrt_runtime_status_t runner_handle_frame(void *user_data,
                                                 const rvrt_frame_t *frame)
{
    runner_decode_context_t *const decode = user_data;
    if ((decode == NULL) || (decode->runner == NULL) || (frame == NULL)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    const rvrt_runtime_status_t timestamp_status =
        runner_validate_frame_timestep(decode, frame);
    if (timestamp_status != RVRT_RUNTIME_OK) {
        return timestamp_status;
    }
    if (decode->runner->has_fast_data_layout ||
        decode->runner->has_fast_voltage_layout) {
        return runner_handle_codec_result(
            decode, rvrt_decode_output_fast_frame(
                        &decode->runner->output_view, frame,
                        decode->runner->runtime.timesteps, decode->output,
                        decode->output_capacity, decode->output_stride,
                        decode->runner->voltage_state,
                        decode->runner->voltage_state_capacity));
    }
    return runner_handle_codec_result(
        decode, rvrt_decode_output_frames_incremental(
                    &decode->runner->output_view, frame, 1U,
                    decode->runner->runtime.timesteps, decode->output,
                    decode->output_capacity, decode->output_stride,
                    decode->runner->voltage_state,
                    decode->runner->voltage_state_capacity));
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

static rvrt_runtime_status_t
validate_input_schedule(const rvrt_artifact_input_mapping_view_t *view,
                        uint32_t total_timesteps)
{
    if ((view == NULL) || (view->entries == NULL) ||
        (view->entry_count == 0U)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    for (uint32_t index = 0U; index < view->entry_count; ++index) {
        rvrt_artifact_input_entry_t entry = {0};
        if (rvrt_artifact_input_mapping_entry(view, index, &entry) !=
            RVRT_ARTIFACT_OK) {
            return RVRT_RUNTIME_RUNTIME_ERROR;
        }
        if (entry.target_lcn >= 8U) {
            return RVRT_RUNTIME_RUNTIME_ERROR;
        }
        const uint32_t tick_relative_capacity = 1U << entry.target_lcn;
        if (entry.tick_relative >= tick_relative_capacity) {
            return RVRT_RUNTIME_RUNTIME_ERROR;
        }
        const uint32_t timestamp_capacity = 1U << (8U - entry.target_lcn);
        if (total_timesteps > timestamp_capacity) {
            return RVRT_RUNTIME_SCHEDULE_UNSUPPORTED;
        }
    }
    return RVRT_RUNTIME_OK;
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

rvrt_runtime_status_t
rvrt_thread_runner_open(rvrt_thread_runner_t *runner,
                        const rvrt_thread_runner_open_config_t *config)
{
    if ((runner == NULL) || (config == NULL) || (config->session == NULL) ||
        !config->session->opened || !config->session->configured ||
        (config->timeout_ms == 0U) || (config->session->faulted)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (runner->opened) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    memset(runner, 0, sizeof(*runner));
    runner->session = config->session;
    runner->artifact = &config->session->artifact;
    runner->thread_index = config->thread_index;
    runner->input_mapping_index = config->input_mapping_index;
    runner->output_mapping_index = config->output_mapping_index;
    runner->voltage_state = config->voltage_state;
    runner->voltage_state_capacity = config->voltage_state_capacity;
    runner->timeout_ms = config->timeout_ms;
    runner->encode_frame_capacity = config->session->transport.rx_capacity;

    if ((rvrt_artifact_thread_runtime(runner->artifact, runner->thread_index,
                                      &runner->runtime) != RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_input_mapping_view(
             runner->artifact, runner->thread_index,
             runner->input_mapping_index,
             &runner->input_view) != RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_output_mapping_view(
             runner->artifact, runner->thread_index,
             runner->output_mapping_index,
             &runner->output_view) != RVRT_ARTIFACT_OK) ||
        (runner->runtime.timesteps == 0U) ||
        (runner->runtime.pipeline_latency == 0U) ||
        (runner->runtime.output_time_encoding !=
         RVRT_OUTPUT_TIME_ENCODING_STREAM) ||
        (runner->runtime.completion_sync_timestep > 0xFFFFFFU) ||
        (runner->input_view.element_count == 0U) ||
        (runner->output_view.element_count == 0U) ||
        (runner->output_view.target_lcn > 7U)) {
        memset(runner, 0, sizeof(*runner));
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    runner->input_row_bytes = runner->input_view.element_count;
    if (runner->output_view.kind == RVRT_OUTPUT_DATA) {
        runner->output_row_bytes = runner->output_view.element_count;
    } else if ((runner->output_view.kind == RVRT_OUTPUT_VOLTAGE) &&
               (sizeof(int32_t) <=
                SIZE_MAX / runner->output_view.element_count)) {
        runner->output_row_bytes =
            (size_t)runner->output_view.element_count * sizeof(int32_t);
    } else {
        memset(runner, 0, sizeof(*runner));
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    rvrt_runtime_status_t status =
        validate_input_schedule(&runner->input_view, runner->runtime.timesteps);
    if (status != RVRT_RUNTIME_OK) {
        memset(runner, 0, sizeof(*runner));
        return status;
    }

    uint32_t required_voltage_state = 0U;
    if ((runner->output_view.kind == RVRT_OUTPUT_VOLTAGE) &&
        (!voltage_state_capacity(runner->runtime.timesteps,
                                 runner->output_view.element_count,
                                 &required_voltage_state) ||
         (runner->voltage_state == NULL) ||
         (runner->voltage_state_capacity < required_voltage_state))) {
        memset(runner, 0, sizeof(*runner));
        return RVRT_RUNTIME_BUFFER_TOO_SMALL;
    }

    if (rvrt_output_fast_layout(
            &runner->output_view, &runner->has_fast_data_layout,
            &runner->has_fast_voltage_layout) != RVRT_CODEC_STATUS_OK) {
        memset(runner, 0, sizeof(*runner));
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    runner->opened = true;
    runner->session->runner_count++;
    return RVRT_RUNTIME_OK;
}

rvrt_runtime_status_t rvrt_thread_runner_close(rvrt_thread_runner_t *runner)
{
    if (runner == NULL) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (!runner->opened) {
        return RVRT_RUNTIME_OK;
    }
    if (runner->active) {
        return RVRT_RUNTIME_BUSY;
    }
    if ((runner->session == NULL) || (runner->session->runner_count == 0U)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    runner->session->runner_count--;
    memset(runner, 0, sizeof(*runner));
    return RVRT_RUNTIME_OK;
}

rvrt_runtime_status_t
rvrt_thread_runner_get_stats(const rvrt_thread_runner_t *runner,
                             rvrt_runtime_stats_t *stats)
{
    if ((runner == NULL) || !runner->opened || (runner->session == NULL)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    return rvrt_runtime_session_get_stats(runner->session, stats);
}

rvrt_runtime_status_t rvrt_thread_runner_run_sample_profiled(
    rvrt_thread_runner_t *runner, const uint8_t *input, size_t input_capacity,
    size_t input_stride, void *output, size_t output_capacity,
    size_t output_stride, rvrt_thread_runner_sample_timing_t *timing)
{
    if ((runner == NULL) || !runner->opened || (runner->session == NULL) ||
        (input == NULL) || (output == NULL) ||
        (runner->encode_frame_capacity == 0U)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
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
            return RVRT_RUNTIME_BUFFER_TOO_SMALL;
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
    uint8_t *const output_bytes = (uint8_t *)output;
    if (!sample_region_fits(input_capacity, runner->input_row_bytes,
                            effective_input_stride,
                            runner->runtime.timesteps) ||
        !sample_region_fits(output_capacity, runner->output_row_bytes,
                            effective_output_stride,
                            runner->runtime.timesteps)) {
        return RVRT_RUNTIME_BUFFER_TOO_SMALL;
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
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    uint32_t required_voltage_state = 0U;
    if (runner->output_view.kind == RVRT_OUTPUT_VOLTAGE) {
        if ((((uintptr_t)output_bytes % sizeof(int32_t)) != 0U) ||
            ((output_capacity % sizeof(int32_t)) != 0U) ||
            ((effective_output_stride % sizeof(int32_t)) != 0U) ||
            !voltage_state_capacity(runner->runtime.timesteps,
                                    runner->output_view.element_count,
                                    &required_voltage_state)) {
            return RVRT_RUNTIME_RUNTIME_ERROR;
        }
        memset(runner->voltage_state, 0,
               (size_t)required_voltage_state * sizeof(*runner->voltage_state));
    }

    rvrt_runtime_status_t status =
        rvrt_runtime_session_begin(runner->session, runner);
    if (status != RVRT_RUNTIME_OK) {
        return status;
    }
    runner->active = true;
    runner->sync_mode = RVRT_RUNTIME_SYNC_MODE_UNSET;
    runner->completed_timesteps = 0U;

    runner_decode_context_t decode = {
        .runner = runner,
        .output = output_bytes,
        .output_capacity = output_capacity,
        .output_stride = effective_output_stride,
        .submitted_timesteps = 0U,
    };
    const rvrt_runtime_rx_frame_handler_t rx_handler = runner_handle_frame;

#if RVRT_ENABLE_STATS
    const rv_counter_t init_start = __get_rv_cycle();
#endif
    status = rvrt_transport_reset_model_for_thread(
        &runner->session->transport, runner->thread_index, runner->timeout_ms,
        &runner->sync_mode, &runner->completed_timesteps);
#if RVRT_ENABLE_STATS
    if (timing != NULL) {
        timing->init_round_trip_cycles = __get_rv_cycle() - init_start;
    }
#endif
    if (status != RVRT_RUNTIME_OK) {
        goto done;
    }

    for (uint32_t timestep = 0U; timestep < runner->runtime.timesteps;
         ++timestep) {
        const uint8_t *const input_row =
            input + (size_t)timestep * effective_input_stride;
        status = rvrt_transport_send_input_timestep(
            &runner->session->transport, &runner->input_view, timestep,
            input_row, runner->input_row_bytes,
            runner->session->transport.rx_frames,
            runner->encode_frame_capacity);
        if (status != RVRT_RUNTIME_OK) {
            goto done;
        }
        memset(output_bytes + (size_t)timestep * effective_output_stride, 0,
               runner->output_row_bytes);
        decode.submitted_timesteps = timestep + 1U;
#if RVRT_ENABLE_STATS
        const rv_counter_t sync_start = __get_rv_cycle();
#endif
        status = rvrt_transport_sync_wait_until_for_thread(
            &runner->session->transport, runner->thread_index, timestep + 1U,
            runner->timeout_ms, &runner->sync_mode,
            &runner->completed_timesteps, NULL, NULL, rx_handler, &decode);
#if RVRT_ENABLE_STATS
        if (timing != NULL) {
            timing->sync_round_trip_cycles[timing->sync_round_trip_count++] =
                __get_rv_cycle() - sync_start;
        }
#endif
        if (status != RVRT_RUNTIME_OK) {
            goto done;
        }
    }

    if (runner->runtime.completion_sync_timestep > runner->runtime.timesteps) {
#if RVRT_ENABLE_STATS
        const rv_counter_t sync_start = __get_rv_cycle();
#endif
        status = rvrt_transport_sync_wait_until_for_thread(
            &runner->session->transport, runner->thread_index,
            runner->runtime.completion_sync_timestep, runner->timeout_ms,
            &runner->sync_mode, &runner->completed_timesteps, NULL, NULL,
            rx_handler, &decode);
#if RVRT_ENABLE_STATS
        if (timing != NULL) {
            timing->sync_round_trip_cycles[timing->sync_round_trip_count++] =
                __get_rv_cycle() - sync_start;
        }
#endif
    }

done:
    rvrt_runtime_session_note_status(runner->session, status);
    runner->active = false;
    rvrt_runtime_session_end(runner->session, runner);
    return status;
}

rvrt_runtime_status_t
rvrt_thread_runner_run_sample(rvrt_thread_runner_t *runner,
                              const uint8_t *input, size_t input_capacity,
                              size_t input_stride, void *output,
                              size_t output_capacity, size_t output_stride)
{
    return rvrt_thread_runner_run_sample_profiled(
        runner, input, input_capacity, input_stride, output, output_capacity,
        output_stride, NULL);
}
