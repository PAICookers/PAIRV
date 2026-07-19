#include "artifact_executor.h"

#include <stddef.h>
#include <string.h>

#include "rvrt_tasks.h"
#include "session_io.h"

static rvrt_session_status_t
executor_buffer_at(rvrt_artifact_executor_t *executor, uint32_t buffer_index,
                   rvrt_executor_buffer_t **buffer, size_t *bytes)
{
    if (__RARELY((executor == NULL) || (buffer == NULL) ||
                 (buffer_index >= executor->buffer_count))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    rvrt_executor_buffer_t *const selected = &executor->buffers[buffer_index];
    if (__RARELY((selected->data == NULL) || (selected->capacity == 0U))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (bytes != NULL) {
        uint32_t artifact_bytes = 0U;
        if (__RARELY(rvrt_artifact_runtime_buffer_bytes(
                         executor->session->artifact, buffer_index,
                         &artifact_bytes, NULL, NULL,
                         NULL) != RVRT_ARTIFACT_OK)) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        *bytes = artifact_bytes;
    }
    *buffer = selected;
    return RVRT_SESSION_OK;
}

static rvrt_session_status_t
validate_execution_plan(const rvrt_artifact_executor_config_t *config,
                        uint32_t *input_ref, uint32_t *output_ref)
{
    const rvrt_artifact_t *const artifact = config->session->artifact;
    uint32_t buffer_count = 0U;
    rvrt_artifact_status_t artifact_status =
        rvrt_artifact_runtime_buffer_count(artifact, &buffer_count);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                 (buffer_count == 0U))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (__RARELY(config->buffer_count < buffer_count)) {
        return RVRT_SESSION_BUFFER_TOO_SMALL;
    }
    for (uint32_t i = 0U; i < buffer_count; ++i) {
        uint32_t required_bytes = 0U;
        artifact_status = rvrt_artifact_runtime_buffer_bytes(
            artifact, i, &required_bytes, NULL, NULL, NULL);
        if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        if (__RARELY((config->buffers[i].data == NULL) ||
                     (config->buffers[i].capacity < required_bytes))) {
            return RVRT_SESSION_BUFFER_TOO_SMALL;
        }
    }

    uint32_t task_count = 0U;
    artifact_status = rvrt_artifact_cpu_task_count(artifact, &task_count);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    for (uint32_t i = 0U; i < task_count; ++i) {
        rvrt_artifact_cpu_task_t task = {0};
        artifact_status = rvrt_artifact_cpu_task(artifact, i, &task);
        if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                     (task.input_ref >= buffer_count) ||
                     (task.output_ref >= buffer_count))) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
    }
    rvrt_artifact_runtime_target_t target = {0};
    artifact_status = rvrt_artifact_runtime_target(artifact, &target);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                 (target.required_task_abi_version != RVRT_TASK_ABI_VERSION) ||
                 (task_count > RVRT_TASK_COUNT))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    rvrt_artifact_runtime_t runtime = {0};
    artifact_status = rvrt_artifact_thread_runtime(
        artifact, config->session->thread_index, &runtime);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                 ((task_count != 0U) && (runtime.timesteps != 1U)))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    uint32_t phase_count = 0U;
    artifact_status = rvrt_artifact_paicore_phase_count(artifact, &phase_count);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                 (phase_count == 0U))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    for (uint32_t i = 0U; i < phase_count; ++i) {
        rvrt_artifact_paicore_phase_t phase = {0};
        artifact_status = rvrt_artifact_paicore_phase(artifact, i, &phase);
        rvrt_artifact_input_mapping_view_t input_view = {0};
        rvrt_artifact_output_mapping_view_t output_view = {0};
        if ((artifact_status == RVRT_ARTIFACT_OK) &&
            ((phase.input_ref >= buffer_count) ||
             (phase.output_ref >= buffer_count))) {
            artifact_status = RVRT_ARTIFACT_OUT_OF_RANGE;
        }
        if (artifact_status == RVRT_ARTIFACT_OK) {
            artifact_status = rvrt_artifact_get_input_mapping_view(
                artifact, config->session->thread_index,
                phase.input_mapping_ref, &input_view);
        }
        if (artifact_status == RVRT_ARTIFACT_OK) {
            artifact_status = rvrt_artifact_get_output_mapping_view(
                artifact, config->session->thread_index,
                phase.output_mapping_ref, &output_view);
        }
        if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
    }

    uint32_t stage_count = 0U;
    artifact_status = rvrt_artifact_stage_count(artifact, &stage_count);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                 (stage_count == 0U))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    uint32_t previous_output_ref = 0U;
    for (uint32_t i = 0U; i < stage_count; ++i) {
        rvrt_artifact_stage_t stage = {0};
        artifact_status = rvrt_artifact_stage(artifact, i, &stage);
        if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                     (stage.stage_index != i) ||
                     ((i == 0U) && (stage.kind != RVRT_STAGE_PAICORE)))) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }

        uint32_t stage_input_ref = 0U;
        uint32_t stage_output_ref = 0U;
        artifact_status = rvrt_artifact_stage_buffer_refs(
            artifact, &stage, &stage_input_ref, &stage_output_ref);
        if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                     (stage_input_ref >= buffer_count) ||
                     (stage_output_ref >= buffer_count) ||
                     ((i > 0U) && (stage_input_ref != previous_output_ref)))) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }

        if (i == 0U) {
            *input_ref = stage_input_ref;
        }
        previous_output_ref = stage_output_ref;
    }

    *output_ref = previous_output_ref;
    return RVRT_SESSION_OK;
}

static rvrt_session_status_t
decode_data(const rvrt_artifact_output_mapping_view_t *view,
            const rvrt_frame_t *frames, uint32_t frame_count, uint8_t *output,
            size_t output_size)
{
    for (uint32_t i = 0U; i < frame_count; ++i) {
        bool written = false;
        if (__RARELY(rvrt_decode_output_frame(view, &frames[i], output,
                                              output_size,
                                              &written) != RVRT_STATUS_OK)) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
    }
    return RVRT_SESSION_OK;
}

static rvrt_session_status_t
decode_voltage(rvrt_artifact_executor_t *executor,
               const rvrt_artifact_output_mapping_view_t *view,
               const rvrt_frame_t *frames, uint32_t frame_count,
               uint8_t *output, size_t output_size)
{
    if (__RARELY(((output_size % sizeof(int32_t)) != 0U) ||
                 (((uintptr_t)output % sizeof(int32_t)) != 0U))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    if (output_size / sizeof(int32_t) > UINT32_MAX) {
        return RVRT_SESSION_BUFFER_TOO_SMALL;
    }
    const uint32_t output_count = (uint32_t)(output_size / sizeof(int32_t));
    if (__RARELY((executor->voltage_state == NULL) ||
                 (executor->voltage_state_capacity < output_count))) {
        return RVRT_SESSION_BUFFER_TOO_SMALL;
    }
    memset(executor->voltage_state, 0,
           output_count * sizeof(executor->voltage_state[0]));

    int32_t *const voltage_output = (int32_t *)(void *)output;
    for (uint32_t i = 0U; i < frame_count; ++i) {
        bool written = false;
        if (__RARELY(rvrt_decode_voltage_frame(
                         view, &frames[i], voltage_output, output_count,
                         executor->voltage_state, output_count,
                         &written) != RVRT_STATUS_OK)) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
    }
    for (uint32_t i = 0U; i < output_count; ++i) {
        if (__RARELY(executor->voltage_state[i].received_mask != 0U)) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }
    }
    return RVRT_SESSION_OK;
}

static rvrt_session_status_t
run_paicore_phase(rvrt_artifact_executor_t *executor, uint32_t phase_index,
                  uint32_t timeout_ms)
{
    const rvrt_artifact_t *const artifact = executor->session->artifact;
    rvrt_artifact_paicore_phase_t phase = {0};
    rvrt_artifact_status_t artifact_status =
        rvrt_artifact_paicore_phase(artifact, phase_index, &phase);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    rvrt_executor_buffer_t *input = NULL;
    rvrt_executor_buffer_t *output = NULL;
    size_t input_bytes = 0U;
    size_t output_bytes = 0U;
    rvrt_session_status_t status =
        executor_buffer_at(executor, phase.input_ref, &input, &input_bytes);
    if (status == RVRT_SESSION_OK) {
        status = executor_buffer_at(executor, phase.output_ref, &output,
                                    &output_bytes);
    }
    if (__RARELY(status != RVRT_SESSION_OK)) {
        return status;
    }

    rvrt_artifact_input_mapping_view_t input_view = {0};
    rvrt_artifact_output_mapping_view_t output_view = {0};
    artifact_status = rvrt_artifact_get_input_mapping_view(
        artifact, executor->session->thread_index, phase.input_mapping_ref,
        &input_view);
    if (artifact_status == RVRT_ARTIFACT_OK) {
        artifact_status = rvrt_artifact_get_output_mapping_view(
            artifact, executor->session->thread_index, phase.output_mapping_ref,
            &output_view);
    }
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    memset(output->data, 0, output_bytes);
    status = rvrt_session_send_input_timestep(
        executor->session, &input_view, 0U, input->data, input_bytes,
        executor->workspace, executor->workspace_capacity);
    if (__RARELY(status != RVRT_SESSION_OK)) {
        return status;
    }

    const rvrt_frame_t *rx_frames = NULL;
    uint32_t rx_frame_count = 0U;
    status = rvrt_session_sync_wait(executor->session, phase.latency_ticks,
                                    timeout_ms, &rx_frames, &rx_frame_count);
    if (__RARELY(status != RVRT_SESSION_OK)) {
        return status;
    }

    if (output_view.kind == RVRT_OUTPUT_DATA) {
        return decode_data(&output_view, rx_frames, rx_frame_count,
                           output->data, output_bytes);
    }
    if (output_view.kind == RVRT_OUTPUT_VOLTAGE) {
        return decode_voltage(executor, &output_view, rx_frames, rx_frame_count,
                              output->data, output_bytes);
    }
    return RVRT_SESSION_RUNTIME_ERROR;
}

static rvrt_session_status_t run_cpu_task(rvrt_artifact_executor_t *executor,
                                          uint32_t task_index)
{
    const rvrt_artifact_t *const artifact = executor->session->artifact;
    rvrt_artifact_cpu_task_t task = {0};
    if (__RARELY(rvrt_artifact_cpu_task(artifact, task_index, &task) !=
                 RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    rvrt_executor_buffer_t *input = NULL;
    rvrt_executor_buffer_t *output = NULL;
    size_t input_bytes = 0U;
    size_t output_bytes = 0U;
    rvrt_session_status_t status =
        executor_buffer_at(executor, task.input_ref, &input, &input_bytes);
    if (status == RVRT_SESSION_OK) {
        status = executor_buffer_at(executor, task.output_ref, &output,
                                    &output_bytes);
    }
    if (__RARELY(status != RVRT_SESSION_OK)) {
        return status;
    }

    if (__RARELY((input_bytes > UINT32_MAX) || (output_bytes > UINT32_MAX))) {
        return RVRT_SESSION_BUFFER_TOO_SMALL;
    }

    memset(output->data, 0, output_bytes);
    const rvrt_task_io_t task_io = {input->data, (uint32_t)input_bytes,
                                    output->data, (uint32_t)output_bytes};
    if (__RARELY(rvrt_task_run(task_index, &task_io) != RVRT_TASK_STATUS_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    return RVRT_SESSION_OK;
}

rvrt_session_status_t
rvrt_artifact_executor_init(rvrt_artifact_executor_t *executor,
                            const rvrt_artifact_executor_config_t *config)
{
    if (__RARELY(
            (executor == NULL) || (config == NULL) ||
            (config->session == NULL) || (config->session->artifact == NULL) ||
            (config->buffers == NULL) || (config->buffer_count == 0U) ||
            (config->workspace == NULL) || (config->workspace_capacity == 0U) ||
            (config->workspace_capacity > RVRT_MAX_WORKSPACE_FRAMES))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    uint32_t input_ref = 0U;
    uint32_t output_ref = 0U;
    rvrt_session_status_t status =
        validate_execution_plan(config, &input_ref, &output_ref);
    if (__RARELY(status != RVRT_SESSION_OK)) {
        return status;
    }

    rvrt_artifact_capacity_t capacity = {0};
    if (__RARELY(rvrt_artifact_get_capacity(config->session->artifact,
                                            config->session->thread_index,
                                            &capacity) != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (__RARELY(config->session->rx_capacity < capacity.rx_frame_count)) {
        return RVRT_SESSION_BUFFER_TOO_SMALL;
    }

    executor->session = config->session;
    executor->buffers = config->buffers;
    executor->buffer_count = config->buffer_count;
    executor->workspace = config->workspace;
    executor->workspace_capacity = config->workspace_capacity;
    executor->voltage_state = config->voltage_state;
    executor->voltage_state_capacity = config->voltage_state_capacity;
    executor->input_ref = input_ref;
    executor->input_bytes = capacity.input_bytes;
    executor->output_ref = output_ref;
    executor->output_bytes = capacity.final_output_bytes;
    return RVRT_SESSION_OK;
}

rvrt_session_status_t
rvrt_artifact_executor_run(rvrt_artifact_executor_t *executor,
                           const uint8_t *input, size_t input_size,
                           uint32_t timeout_ms)
{
    if (__RARELY((executor == NULL) || (input == NULL))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (__RARELY(input_size != executor->input_bytes)) {
        return (input_size < executor->input_bytes)
                   ? RVRT_SESSION_BUFFER_TOO_SMALL
                   : RVRT_SESSION_RUNTIME_ERROR;
    }

    rvrt_executor_buffer_t *entry_input = NULL;
    rvrt_session_status_t status =
        executor_buffer_at(executor, executor->input_ref, &entry_input, NULL);
    if (__RARELY(status != RVRT_SESSION_OK)) {
        return status;
    }
    memmove(entry_input->data, input, input_size);

    uint32_t stage_count = 0U;
    if (__RARELY(rvrt_artifact_stage_count(executor->session->artifact,
                                           &stage_count) != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    for (uint32_t i = 0U; i < stage_count; ++i) {
        rvrt_artifact_stage_t stage = {0};
        if (__RARELY(rvrt_artifact_stage(executor->session->artifact, i,
                                         &stage) != RVRT_ARTIFACT_OK)) {
            return RVRT_SESSION_RUNTIME_ERROR;
        }

        if (stage.kind == RVRT_STAGE_PAICORE) {
            status = run_paicore_phase(executor, stage.ref_index, timeout_ms);
        } else if (stage.kind == RVRT_STAGE_CPU_TASK) {
            status = run_cpu_task(executor, stage.ref_index);
        } else {
            status = RVRT_SESSION_RUNTIME_ERROR;
        }
        if (__RARELY(status != RVRT_SESSION_OK)) {
            return status;
        }
    }
    return RVRT_SESSION_OK;
}

rvrt_session_status_t
rvrt_artifact_executor_get_output(const rvrt_artifact_executor_t *executor,
                                  const uint8_t **data, size_t *size)
{
    if (__RARELY((executor == NULL) || (data == NULL) || (size == NULL) ||
                 (executor->output_ref >= executor->buffer_count) ||
                 (executor->buffers[executor->output_ref].data == NULL))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    *data = executor->buffers[executor->output_ref].data;
    *size = executor->output_bytes;
    return RVRT_SESSION_OK;
}
