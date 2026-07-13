#include "session.h"

#include <stddef.h>
#include <string.h>

#include "debug.h"
#include "evalsoc_noc.h"
#include "rvrt_tasks.h"

static rvrt_session_t *g_active_session;

#define RVRT_SESSION_COMPLETE_FRAME_COUNT 1U

void paicore_noc_handler(void);

static rvrt_session_status_t register_irq(rvrt_session_t *session)
{
    if (__RARELY(session == NULL)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    noc_irq_disable();
    g_active_session = session;
    const int32_t result =
        ECLIC_Register_IRQ(PAICORE_NOC_IRQn, ECLIC_NON_VECTOR_INTERRUPT,
                           ECLIC_LEVEL_TRIGGER, 1U, 0U, paicore_noc_handler);
    if (__RARELY(result != 0)) {
        g_active_session = NULL;
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    __enable_irq();
    return RVRT_SESSION_STATUS_OK;
}

static uint32_t required_rx_capacity(uint32_t output_frames)
{
    if (output_frames > (UINT32_MAX - RVRT_SESSION_COMPLETE_FRAME_COUNT)) {
        return UINT32_MAX;
    }
    return output_frames + RVRT_SESSION_COMPLETE_FRAME_COUNT;
}

static bool expected_output_frame_count(
    const rvrt_artifact_output_mapping_view_t *view, uint32_t *frame_count)
{
    if ((view == NULL) || (frame_count == NULL)) {
        return false;
    }

    uint32_t frames_per_entry = 1U;
    if (view->kind == RVRT_OUTPUT_VOLTAGE) {
        if ((view->bit_width == 0U) || ((view->bit_width % 8U) != 0U)) {
            return false;
        }
        frames_per_entry = view->bit_width / 8U;
    }

    if (view->entry_count > (UINT32_MAX / frames_per_entry)) {
        return false;
    }
    *frame_count = view->entry_count * frames_per_entry;
    return true;
}

static bool is_int32_aligned(const void *data)
{
    return (((uintptr_t)data) % sizeof(int32_t)) == 0U;
}

static rv_counter_t timeout_cycles(uint32_t timeout_ms)
{
    return ((rv_counter_t)(SystemCoreClock / 1000U)) * timeout_ms;
}

static void clear_stats(rvrt_session_stats_t *stats)
{
    *stats = (rvrt_session_stats_t){0};
}

static void clear_phase(rvrt_session_phase_t *phase)
{
    phase->armed = false;
    phase->done = false;
    phase->overflow = false;
    phase->hardware_error = false;
    phase->rx_count = 0U;
    phase->output_work_count = 0U;
    phase->complete_count = 0U;
}

/**
 * @brief Publish a fresh IRQ phase before its synchronization frame is sent.
 *
 * The write barrier makes cleared counters visible before armed becomes true.
 */
static void arm_phase(rvrt_session_t *session)
{
    rvrt_session_phase_t *const phase = &session->phase;

    noc_irq_disable();
    noc_irq_ack();
    clear_phase(phase);
    __WMB();
    phase->armed = true;
    noc_irq_enable();
}

/**
 * @brief Wait for the IRQ handler to observe a completion frame or failure.
 *
 * This function owns the transition from armed to disarmed for one phase.
 */
static rvrt_session_status_t wait_phase(rvrt_session_t *session,
                                        uint32_t timeout_ms,
                                        rv_counter_t *cycles_out)
{
    rvrt_session_phase_t *const phase = &session->phase;
    const rv_counter_t start_cycles = __get_rv_cycle();
    const rv_counter_t limit_cycles = timeout_cycles(timeout_ms);

    while (__USUALLY(!phase->done)) {
        if (__RARELY((__get_rv_cycle() - start_cycles) > limit_cycles)) {
            noc_irq_disable();
            RV_DEBUG_LOGE(
                "runtime",
                "phase timeout pending=%u enabled=%u rx=%u work=%u "
                "complete=%u",
                (unsigned)noc_irq_pending(), (unsigned)noc_irq_is_enabled(),
                (unsigned)phase->rx_count, (unsigned)phase->output_work_count,
                (unsigned)phase->complete_count);
            phase->armed = false;
            phase->hardware_error = true;
            return RVRT_SESSION_STATUS_TIMEOUT;
        }
    }

    noc_irq_disable();
    __RMB();
    phase->armed = false;
    if (cycles_out != NULL) {
        *cycles_out = __get_rv_cycle() - start_cycles;
    }
    if (__RARELY(phase->overflow)) {
        return RVRT_SESSION_STATUS_OVERFLOW;
    }
    if (__RARELY(phase->hardware_error)) {
        return RVRT_SESSION_STATUS_HARDWARE_ERROR;
    }
    return RVRT_SESSION_STATUS_OK;
}

static void collect_phase_stats(rvrt_session_t *session)
{
#if RVRT_SESSION_ENABLE_STATS
    const rvrt_session_phase_t *const phase = &session->phase;
    session->stats.rx_frames += phase->rx_count;
    session->stats.output_work_frames += phase->output_work_count;
    session->stats.complete_frames += phase->complete_count;
    if (phase->overflow) {
        session->stats.overflow = true;
    }
    if (phase->hardware_error) {
        session->stats.hardware_error = true;
    }
#else
    (void)session;
#endif
}

static rvrt_session_status_t session_buffer_at(rvrt_session_t *session,
                                               uint32_t buffer_index,
                                               rvrt_session_buffer_t **buffer)
{
    if (__RARELY((session == NULL) || (buffer == NULL) ||
                 (buffer_index >= session->buffer_count))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    rvrt_session_buffer_t *const selected = &session->buffers[buffer_index];
    if (__RARELY((selected->data == NULL) || (selected->capacity == 0U))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    *buffer = selected;
    return RVRT_SESSION_STATUS_OK;
}

/**
 * @brief Validate ordered-stage references and derive external buffers.
 *
 * The accepted v7b plan starts with PAICORE and has contiguous stage buffers.
 */
static rvrt_session_status_t
validate_execution_plan(const rvrt_session_config_t *config,
                        uint32_t *input_ref, uint32_t *output_ref)
{
    uint32_t artifact_buffer_count = 0U;
    rvrt_artifact_status_t artifact_status = rvrt_artifact_runtime_buffer_count(
        config->artifact, &artifact_buffer_count);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                 (artifact_buffer_count == 0U))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    uint32_t task_count = 0U;
    artifact_status =
        rvrt_artifact_cpu_task_count(config->artifact, &task_count);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    for (uint32_t i = 0U; i < task_count; ++i) {
        rvrt_artifact_cpu_task_t task = {0};
        artifact_status = rvrt_artifact_cpu_task(config->artifact, i, &task);
        if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                     (task.input_ref >= artifact_buffer_count) ||
                     (task.output_ref >= artifact_buffer_count))) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
    }

    uint32_t phase_count = 0U;
    artifact_status =
        rvrt_artifact_paicore_phase_count(config->artifact, &phase_count);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                 (phase_count == 0U))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    rvrt_artifact_runtime_t runtime = {0};
    artifact_status = rvrt_artifact_thread_runtime(
        config->artifact, config->thread_index, &runtime);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    if (__RARELY((task_count != 0U) && (runtime.timesteps != 1U))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    for (uint32_t i = 0U; i < phase_count; ++i) {
        rvrt_artifact_paicore_phase_t phase = {0};
        artifact_status =
            rvrt_artifact_paicore_phase(config->artifact, i, &phase);
        rvrt_artifact_input_mapping_view_t input_view = {0};
        rvrt_artifact_output_mapping_view_t output_view = {0};
        if ((artifact_status == RVRT_ARTIFACT_OK) &&
            ((phase.input_ref >= artifact_buffer_count) ||
             (phase.output_ref >= artifact_buffer_count))) {
            artifact_status = RVRT_ARTIFACT_OUT_OF_RANGE;
        }
        if (artifact_status == RVRT_ARTIFACT_OK) {
            artifact_status = rvrt_artifact_get_input_mapping_view(
                config->artifact, config->thread_index, phase.input_mapping_ref,
                &input_view);
        }
        if (artifact_status == RVRT_ARTIFACT_OK) {
            artifact_status = rvrt_artifact_get_output_mapping_view(
                config->artifact, config->thread_index,
                phase.output_mapping_ref, &output_view);
        }
        if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
    }

    uint32_t stage_count = 0U;
    artifact_status = rvrt_artifact_stage_count(config->artifact, &stage_count);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                 (stage_count == 0U))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    uint32_t previous_output_ref = 0U;
    for (uint32_t i = 0U; i < stage_count; ++i) {
        rvrt_artifact_stage_t stage = {0};
        artifact_status = rvrt_artifact_stage(config->artifact, i, &stage);
        if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) ||
                     (stage.stage_index != i))) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
        if (__RARELY((i == 0U) && (stage.kind != RVRT_STAGE_PAICORE))) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }

        uint32_t stage_input_ref = 0U;
        uint32_t stage_output_ref = 0U;
        artifact_status = rvrt_artifact_stage_buffer_refs(
            config->artifact, &stage, &stage_input_ref, &stage_output_ref);
        if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
        if (__RARELY((stage_input_ref >= artifact_buffer_count) ||
                     (stage_output_ref >= artifact_buffer_count) ||
                     ((i > 0U) && (stage_input_ref != previous_output_ref)))) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }

        if (i == 0U) {
            *input_ref = stage_input_ref;
        }
        previous_output_ref = stage_output_ref;
    }

    *output_ref = previous_output_ref;
    return RVRT_SESSION_STATUS_OK;
}

static rvrt_session_status_t
validate_session_buffers(const rvrt_artifact_t *artifact,
                         rvrt_session_buffer_t *buffers, uint32_t buffer_count)
{
    uint32_t artifact_buffer_count = 0U;
    rvrt_artifact_status_t artifact_status =
        rvrt_artifact_runtime_buffer_count(artifact, &artifact_buffer_count);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    if (__RARELY(buffer_count < artifact_buffer_count)) {
        return RVRT_SESSION_STATUS_BUFFER_TOO_SMALL;
    }

    for (uint32_t i = 0U; i < artifact_buffer_count; ++i) {
        uint32_t required_bytes = 0U;
        artifact_status = rvrt_artifact_runtime_buffer_bytes(
            artifact, i, &required_bytes, NULL, NULL, NULL);
        if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
        if (__RARELY((buffers[i].data == NULL) ||
                     (buffers[i].capacity < required_bytes))) {
            return RVRT_SESSION_STATUS_BUFFER_TOO_SMALL;
        }
    }
    return RVRT_SESSION_STATUS_OK;
}

rvrt_session_status_t rvrt_session_load_config(const rvrt_artifact_t *artifact)
{
    if (__RARELY(artifact == NULL)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    uint32_t word_count = 0U;
    rvrt_artifact_status_t artifact_status =
        rvrt_artifact_config_word_count(artifact, &word_count);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) || (word_count == 0U) ||
                 ((word_count % 2U) != 0U))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    RV_DEBUG_LOGI("runtime", "config load begin words=%u frames=%u",
                  (unsigned)word_count, (unsigned)(word_count / 2U));

    const bool irq_was_enabled = noc_irq_is_enabled();
    noc_irq_disable();

    const uint32_t frame_count = word_count / 2U;
    for (uint32_t frame_index = 0U; frame_index < frame_count; ++frame_index) {
        uint32_t high = 0U;
        uint32_t low = 0U;
        artifact_status = rvrt_artifact_config_frame_words(
            artifact, frame_index, &high, &low);
        if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
            if (irq_was_enabled) {
                noc_irq_enable();
            }
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
        noc_fifo_write_frame_words_unlocked(high, low);
    }

    if (irq_was_enabled) {
        noc_irq_enable();
    }
    RV_DEBUG_LOGI("runtime", "config load done frames=%u",
                  (unsigned)(word_count / 2U));
    return RVRT_SESSION_STATUS_OK;
}

static void record_decoded_write(rvrt_session_t *session, bool written)
{
    if (written) {
#if RVRT_SESSION_ENABLE_STATS
        session->stats.decoded_writes++;
#else
        (void)session;
#endif
    }
}

static rvrt_session_status_t
decode_data_phase(rvrt_session_t *session,
                  const rvrt_artifact_output_mapping_view_t *view,
                  uint8_t *output, uint32_t output_size)
{
    const uint32_t rx_count = session->phase.rx_count;

    for (uint32_t i = 0U; i < rx_count; ++i) {
        bool written = false;
        const rvrt_status_t status = rvrt_decode_output_frame(
            view, &session->rx_frames[i], output, output_size, &written);
        if (__RARELY(status != RVRT_STATUS_OK)) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
        record_decoded_write(session, written);
    }
    return RVRT_SESSION_STATUS_OK;
}

static rvrt_session_status_t
decode_membrane_phase(rvrt_session_t *session,
                      const rvrt_artifact_output_mapping_view_t *view,
                      uint8_t *output, uint32_t output_size)
{
    if (__RARELY(((output_size % sizeof(int32_t)) != 0U) ||
                 !is_int32_aligned(output))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    const uint32_t output_count = output_size / (uint32_t)sizeof(int32_t);
    if (__RARELY((session->membrane_state == NULL) ||
                 (session->membrane_state_capacity < output_count))) {
        return RVRT_SESSION_STATUS_BUFFER_TOO_SMALL;
    }

    memset(session->membrane_state, 0,
           output_count * sizeof(session->membrane_state[0]));
    int32_t *const membrane_output = (int32_t *)(void *)output;
    const uint32_t rx_count = session->phase.rx_count;

    for (uint32_t i = 0U; i < rx_count; ++i) {
        bool written = false;
        const rvrt_status_t status = rvrt_decode_membrane_frame(
            view, &session->rx_frames[i], membrane_output, output_count,
            session->membrane_state, output_count, &written);
        if (__RARELY(status != RVRT_STATUS_OK)) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
        record_decoded_write(session, written);
    }
    return RVRT_SESSION_STATUS_OK;
}

static rvrt_session_status_t
decode_phase(rvrt_session_t *session,
             const rvrt_artifact_output_mapping_view_t *view, uint8_t *output,
             uint32_t output_size)
{
    if (view->kind == RVRT_OUTPUT_DATA) {
        return decode_data_phase(session, view, output, output_size);
    }
    if (view->kind == RVRT_OUTPUT_VOLTAGE) {
        return decode_membrane_phase(session, view, output, output_size);
    }
    return RVRT_SESSION_STATUS_RUNTIME_ERROR;
}

/**
 * @brief Execute one PAICORE phase from input encoding through output decoding.
 *
 * Input is emitted in bounded workspace chunks. The IRQ only records received
 * frames; decoding occurs here after the completion frame is observed.
 */
static rvrt_session_status_t run_paicore_pass(
    rvrt_session_t *session,
    const rvrt_artifact_input_mapping_view_t *input_view,
    const rvrt_artifact_output_mapping_view_t *output_view,
    uint32_t input_timestep, uint32_t sync_step, const uint8_t *input,
    uint32_t input_size, uint8_t *decode_output, uint32_t decode_output_size,
    rvrt_frame_t *workspace, uint32_t workspace_capacity, uint32_t timeout_ms)
{
    uint32_t output_frame_count = 0U;
    if (__RARELY(!expected_output_frame_count(output_view, &output_frame_count))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    if (__RARELY(session->rx_capacity < required_rx_capacity(output_frame_count))) {
        return RVRT_SESSION_STATUS_BUFFER_TOO_SMALL;
    }

    rvrt_frame_t sync_frame = {0};
    rvrt_status_t runtime_status = rvrt_build_sync_frame(
        session->artifact, session->thread_index, &sync_frame);
    if (__RARELY(runtime_status != RVRT_STATUS_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    sync_frame.low = sync_step;
    RV_DEBUG_LOGD("runtime",
                  "paicore pass begin output_entries=%u sync=%08x%08x",
                  (unsigned)output_view->entry_count, (unsigned)sync_frame.high,
                  (unsigned)sync_frame.low);

    rvrt_input_cursor_t cursor = {0};
    rvrt_input_cursor_init(&cursor, input_timestep);

    uint32_t sent_input_frames = 0U;
    while (true) {
        uint32_t chunk_count = 0U;
        runtime_status = rvrt_encode_input_chunk(
            input_view, &cursor, input, input_size, workspace,
            workspace_capacity, &chunk_count);
        if (__RARELY((runtime_status != RVRT_STATUS_DONE) &&
                     (runtime_status != RVRT_STATUS_BUFFER_FULL))) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
        if (__RARELY((runtime_status == RVRT_STATUS_BUFFER_FULL) &&
                     (chunk_count == 0U))) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }

        for (uint32_t i = 0U; i < chunk_count; ++i) {
            noc_fifo_write_frame_words_unlocked(workspace[i].high,
                                                workspace[i].low);
            sent_input_frames++;
#if RVRT_SESSION_ENABLE_STATS
            session->stats.sent_input_frames++;
#endif
        }

        if (runtime_status == RVRT_STATUS_DONE) {
            break;
        }
    }
    RV_DEBUG_LOGD("runtime", "paicore pass input frames=%u",
                  (unsigned)sent_input_frames);

    arm_phase(session);
    noc_fifo_write_frame_words(sync_frame.high, sync_frame.low);

    rv_counter_t phase_cycles = 0U;
    rvrt_session_status_t status =
        wait_phase(session, timeout_ms, &phase_cycles);
    collect_phase_stats(session);
#if RVRT_SESSION_ENABLE_STATS
    session->stats.sync_wait_cycles += phase_cycles;
#else
    (void)phase_cycles;
#endif
    if (__RARELY(status != RVRT_SESSION_STATUS_OK)) {
        return status;
    }

    status =
        decode_phase(session, output_view, decode_output, decode_output_size);
    if (__RARELY(status != RVRT_SESSION_STATUS_OK)) {
        return status;
    }
#if RVRT_SESSION_ENABLE_STATS
    session->stats.sync_phases++;
#endif

    return RVRT_SESSION_STATUS_OK;
}

/**
 * @brief Drain the active PAICORE FIFO into session-owned frame storage.
 *
 * IRQ work is deliberately limited to acknowledgement, bounded capture, and
 * completion detection. Artifact lookup and tensor decoding run outside IRQ.
 */
void paicore_noc_handler(void)
{
    SAVE_IRQ_CSR_CONTEXT();
    noc_irq_ack();
    noc_irq_disable();

    rvrt_session_t *const session = g_active_session;
    if (__RARELY((session == NULL) || !session->phase.armed)) {
        RESTORE_IRQ_CSR_CONTEXT();
        return;
    }

    rvrt_session_phase_t *const phase = &session->phase;

    while (!phase->done) {
        const uint32_t index = phase->rx_count;
        if (__RARELY(index >= session->rx_capacity)) {
            phase->overflow = true;
            phase->hardware_error = true;
            __WMB();
            phase->done = true;
            break;
        }

        uint32_t high = 0U;
        uint32_t low = 0U;
        (void)noc_fifo_read_frame_words(&high, &low);
        session->rx_frames[index].high = high;
        session->rx_frames[index].low = low;
        phase->rx_count = index + 1U;

        const rvrt_frame_t *const frame = &session->rx_frames[index];
        if (rvrt_frame_is_work(frame)) {
            phase->output_work_count++;
        }

        if (rvrt_frame_is_complete(frame)) {
            phase->complete_count++;
            __WMB();
            phase->done = true;
        }
    }

    RESTORE_IRQ_CSR_CONTEXT();
}

rvrt_session_status_t rvrt_session_init(rvrt_session_t *session,
                                        const rvrt_session_config_t *config)
{
    if (__RARELY((session == NULL) || (config == NULL) ||
                 (config->artifact == NULL) || (config->rx_frames == NULL) ||
                 (config->buffers == NULL) || (config->rx_capacity == 0U) ||
                 (config->buffer_count == 0U))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    rvrt_session_status_t session_status = validate_session_buffers(
        config->artifact, config->buffers, config->buffer_count);
    if (__RARELY(session_status != RVRT_SESSION_STATUS_OK)) {
        return session_status;
    }

    uint32_t input_ref = 0U;
    uint32_t output_ref = 0U;
    session_status = validate_execution_plan(config, &input_ref, &output_ref);
    if (__RARELY(session_status != RVRT_SESSION_STATUS_OK)) {
        return session_status;
    }

    rvrt_artifact_capacity_t capacity = {0};
    rvrt_artifact_status_t artifact_status = rvrt_artifact_get_capacity(
        config->artifact, config->thread_index, &capacity);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    if (__RARELY(config->rx_capacity < capacity.rx_frame_count)) {
        return RVRT_SESSION_STATUS_BUFFER_TOO_SMALL;
    }

    uint32_t cpu_task_count = 0U;
    artifact_status =
        rvrt_artifact_cpu_task_count(config->artifact, &cpu_task_count);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    rvrt_artifact_runtime_target_t target = {0};
    artifact_status = rvrt_artifact_runtime_target(config->artifact, &target);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    if (__RARELY((target.required_task_abi_version != RVRT_TASK_ABI_VERSION) ||
                 (cpu_task_count > RVRT_TASK_COUNT))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    session->artifact = config->artifact;
    session->thread_index = config->thread_index;
    session->rx_frames = config->rx_frames;
    session->rx_capacity = config->rx_capacity;
    session->buffers = config->buffers;
    session->buffer_count = config->buffer_count;
    session->input_ref = input_ref;
    session->input_bytes = capacity.input_bytes;
    session->output_ref = output_ref;
    session->output_bytes = capacity.final_output_bytes;
    session->membrane_state = config->membrane_state;
    session->membrane_state_capacity = config->membrane_state_capacity;
#if RVRT_SESSION_ENABLE_STATS
    clear_stats(&session->stats);
#endif
    clear_phase(&session->phase);
    return register_irq(session);
}

static rvrt_session_status_t run_cpu_task(rvrt_session_t *session,
                                          uint32_t task_index)
{
    rvrt_artifact_cpu_task_t task = {0};
    const rvrt_artifact_status_t artifact_status =
        rvrt_artifact_cpu_task(session->artifact, task_index, &task);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    rvrt_session_buffer_t *input = NULL;
    rvrt_session_buffer_t *output = NULL;
    rvrt_session_status_t status =
        session_buffer_at(session, task.input_ref, &input);
    if (__RARELY(status != RVRT_SESSION_STATUS_OK)) {
        return status;
    }
    status = session_buffer_at(session, task.output_ref, &output);
    if (__RARELY(status != RVRT_SESSION_STATUS_OK)) {
        return status;
    }
    uint32_t input_bytes = 0U;
    uint32_t output_bytes = 0U;
    rvrt_artifact_status_t buffer_status = rvrt_artifact_runtime_buffer_bytes(
        session->artifact, task.input_ref, &input_bytes, NULL, NULL, NULL);
    if (buffer_status == RVRT_ARTIFACT_OK) {
        buffer_status = rvrt_artifact_runtime_buffer_bytes(
            session->artifact, task.output_ref, &output_bytes, NULL, NULL,
            NULL);
    }
    if (__RARELY(buffer_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    memset(output->data, 0, output_bytes);

    const rvrt_task_io_t task_io = {input->data, input_bytes, output->data,
                                    output_bytes};
    const rvrt_task_status_t task_status = rvrt_task_run(task_index, &task_io);
    if (__RARELY(task_status != RVRT_TASK_STATUS_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    return RVRT_SESSION_STATUS_OK;
}

static rvrt_session_status_t run_paicore_phase(rvrt_session_t *session,
                                               uint32_t phase_index,
                                               rvrt_frame_t *workspace,
                                               uint32_t workspace_capacity,
                                               uint32_t timeout_ms)
{
    rvrt_artifact_paicore_phase_t phase = {0};
    rvrt_artifact_status_t artifact_status =
        rvrt_artifact_paicore_phase(session->artifact, phase_index, &phase);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    rvrt_session_buffer_t *input = NULL;
    rvrt_session_buffer_t *output = NULL;
    rvrt_session_status_t status =
        session_buffer_at(session, phase.input_ref, &input);
    if (__RARELY(status != RVRT_SESSION_STATUS_OK)) {
        return status;
    }
    status = session_buffer_at(session, phase.output_ref, &output);
    if (__RARELY(status != RVRT_SESSION_STATUS_OK)) {
        return status;
    }
    uint32_t input_bytes = 0U;
    uint32_t output_bytes = 0U;
    rvrt_artifact_status_t buffer_status = rvrt_artifact_runtime_buffer_bytes(
        session->artifact, phase.input_ref, &input_bytes, NULL, NULL, NULL);
    if (buffer_status == RVRT_ARTIFACT_OK) {
        buffer_status = rvrt_artifact_runtime_buffer_bytes(
            session->artifact, phase.output_ref, &output_bytes, NULL, NULL,
            NULL);
    }
    if (__RARELY(buffer_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    memset(output->data, 0, output_bytes);

    rvrt_artifact_input_mapping_view_t input_view = {0};
    rvrt_artifact_output_mapping_view_t output_view = {0};
    artifact_status = rvrt_artifact_get_input_mapping_view(
        session->artifact, session->thread_index, phase.input_mapping_ref,
        &input_view);
    if (artifact_status == RVRT_ARTIFACT_OK) {
        artifact_status = rvrt_artifact_get_output_mapping_view(
            session->artifact, session->thread_index, phase.output_mapping_ref,
            &output_view);
    }
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    return run_paicore_pass(session, &input_view, &output_view, 0U,
                            phase.latency_ticks, input->data, input_bytes,
                            output->data, output_bytes, workspace,
                            workspace_capacity, timeout_ms);
}

rvrt_session_status_t
rvrt_session_run_sample(rvrt_session_t *session, const uint8_t *input,
                        uint32_t input_size, rvrt_frame_t *workspace,
                        uint32_t workspace_capacity, uint32_t timeout_ms)
{
    if (__RARELY((session == NULL) || (input == NULL) || (workspace == NULL) ||
                 (workspace_capacity == 0U))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    const rv_counter_t start_cycles = __get_rv_cycle();

    rvrt_session_buffer_t *entry_input = NULL;
    rvrt_session_status_t status =
        session_buffer_at(session, session->input_ref, &entry_input);
    if (__RARELY(status != RVRT_SESSION_STATUS_OK)) {
        return status;
    }
    if (__RARELY(input_size != session->input_bytes)) {
        return (input_size < session->input_bytes)
                   ? RVRT_SESSION_STATUS_BUFFER_TOO_SMALL
                   : RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    memmove(entry_input->data, input, input_size);

    uint32_t stage_count = 0U;
    rvrt_artifact_status_t artifact_status =
        rvrt_artifact_stage_count(session->artifact, &stage_count);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    RV_DEBUG_LOGD("runtime", "run_sample stages=%u", (unsigned)stage_count);

    for (uint32_t stage_index = 0U; stage_index < stage_count; ++stage_index) {
        rvrt_artifact_stage_t stage = {0};
        artifact_status =
            rvrt_artifact_stage(session->artifact, stage_index, &stage);
        if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }

        if (stage.kind == RVRT_STAGE_PAICORE) {
            status = run_paicore_phase(session, stage.ref_index, workspace,
                                       workspace_capacity, timeout_ms);
        } else if (stage.kind == RVRT_STAGE_CPU_TASK) {
            status = run_cpu_task(session, stage.ref_index);
        } else {
            status = RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }

        if (__RARELY(status != RVRT_SESSION_STATUS_OK)) {
            return status;
        }
    }

#if RVRT_SESSION_ENABLE_STATS
    session->stats.total_cycles = __get_rv_cycle() - start_cycles;
#else
    (void)start_cycles;
#endif
    return RVRT_SESSION_STATUS_OK;
}

rvrt_session_status_t rvrt_session_get_output(const rvrt_session_t *session,
                                              const uint8_t **data,
                                              uint32_t *size)
{
    if (__RARELY((session == NULL) || (data == NULL) || (size == NULL))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    if (__RARELY((session->output_ref >= session->buffer_count) ||
                 (session->buffers[session->output_ref].data == NULL) ||
                 (session->buffers[session->output_ref].capacity == 0U))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    *data = session->buffers[session->output_ref].data;
    *size = session->output_bytes;
    return RVRT_SESSION_STATUS_OK;
}

rvrt_session_status_t rvrt_session_get_stats(const rvrt_session_t *session,
                                             rvrt_session_stats_t *stats)
{
    if (__RARELY((session == NULL) || (stats == NULL))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    clear_stats(stats);
#if RVRT_SESSION_ENABLE_STATS
    *stats = session->stats;
#else
    (void)session;
#endif
    return RVRT_SESSION_STATUS_OK;
}

const char *rvrt_session_status_string(rvrt_session_status_t status)
{
    switch (status) {
        case RVRT_SESSION_STATUS_OK:
            return "ok";
        case RVRT_SESSION_STATUS_TIMEOUT:
            return "timeout";
        case RVRT_SESSION_STATUS_BUFFER_TOO_SMALL:
            return "buffer too small";
        case RVRT_SESSION_STATUS_OVERFLOW:
            return "overflow";
        case RVRT_SESSION_STATUS_HARDWARE_ERROR:
            return "hardware error";
        case RVRT_SESSION_STATUS_RUNTIME_ERROR:
            return "runtime error";
        default:
            return "unknown";
    }
}
