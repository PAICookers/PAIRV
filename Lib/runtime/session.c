#include "session.h"

#include <stddef.h>
#include <string.h>

#include "evalsoc_noc.h"

#ifndef RVRT_SESSION_IRQ_MAX_CYCLES
#define RVRT_SESSION_IRQ_MAX_CYCLES 10000000U
#endif

static rvrt_session_t *g_active_session;

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
    if (output_frames > (UINT32_MAX - RVRT_SESSION_RX_MARGIN)) {
        return UINT32_MAX;
    }
    return output_frames + RVRT_SESSION_RX_MARGIN;
}

static rv_counter_t timeout_cycles(uint32_t timeout_ms)
{
    return ((rv_counter_t)(SystemCoreClock / 1000U)) * timeout_ms;
}

static void clear_stats(rvrt_session_stats_t *stats)
{
    *stats = (rvrt_session_stats_t){0};
}

static void clear_phase(rvrt_session_phase_t *phase,
                        uint32_t expected_output_work_count)
{
    phase->armed = false;
    phase->done = false;
    phase->overflow = false;
    phase->hardware_error = false;
    phase->rx_count = 0U;
    phase->output_work_count = 0U;
    phase->complete_count = 0U;
    phase->expected_output_work_count = expected_output_work_count;
}

static void arm_phase(rvrt_session_t *session,
                      uint32_t expected_output_work_count)
{
    rvrt_session_phase_t *const phase = &session->phase;

    noc_irq_disable();
    noc_irq_ack();
    clear_phase(phase, expected_output_work_count);
    __WMB();
    phase->armed = true;
    noc_irq_enable();
}

static rvrt_session_status_t wait_phase(rvrt_session_t *session,
                                        uint32_t timeout_ms,
                                        rv_counter_t *cycles_out)
{
    rvrt_session_phase_t *const phase = &session->phase;
    const rv_counter_t start_cycles = __get_rv_cycle();
    const rv_counter_t limit_cycles = timeout_cycles(timeout_ms);

    while (!phase->done) {
        if (__RARELY((__get_rv_cycle() - start_cycles) > limit_cycles)) {
            noc_irq_disable();
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

rvrt_session_status_t
rvrt_session_replay_config(const rvrt_artifact_t *artifact)
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

    noc_lock_state_t tx_state;
    const bool irq_was_enabled = noc_irq_is_enabled();
    noc_irq_disable();
    noc_enter_critical(&tx_state);

    for (uint32_t word_index = 0U; word_index < word_count; word_index += 2U) {
        uint32_t high = 0U;
        uint32_t low = 0U;
        artifact_status =
            rvrt_artifact_config_word(artifact, word_index, &high);
        if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
            noc_exit_critical(&tx_state);
            if (irq_was_enabled) {
                noc_irq_enable();
            }
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
        artifact_status =
            rvrt_artifact_config_word(artifact, word_index + 1U, &low);
        if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
            noc_exit_critical(&tx_state);
            if (irq_was_enabled) {
                noc_irq_enable();
            }
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
        noc_fifo_write_frame_words_unlocked(high, low);
    }

    noc_exit_critical(&tx_state);
    if (irq_was_enabled) {
        noc_irq_enable();
    }
    return RVRT_SESSION_STATUS_OK;
}

static rvrt_session_status_t decode_phase(rvrt_session_t *session)
{
    const uint32_t rx_count = session->phase.rx_count;
    for (uint32_t i = 0U; i < rx_count; ++i) {
        bool written = false;
        const rvrt_status_t status = rvrt_decode_output_frame(
            session->artifact, session->thread_index, session->output_index,
            &session->rx_frames[i], session->output, session->output_size,
            &written);
        if (__RARELY(status != RVRT_STATUS_OK)) {
            return RVRT_SESSION_STATUS_RUNTIME_ERROR;
        }
        if (written) {
#if RVRT_SESSION_ENABLE_STATS
            session->stats.decoded_writes++;
#endif
        }
    }
    return RVRT_SESSION_STATUS_OK;
}

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

#if RVRT_SESSION_IRQ_MAX_CYCLES > 0
    const rv_counter_t start_cycles = __get_rv_cycle();
#endif

    while (!phase->done) {
        const uint32_t index = phase->rx_count;
        if (__RARELY(index >= session->rx_capacity)) {
            phase->overflow = true;
            phase->hardware_error = true;
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
            const uint32_t count = phase->output_work_count + 1U;
            phase->output_work_count = count;
            if ((phase->expected_output_work_count != 0U) &&
                (count >= phase->expected_output_work_count)) {
                phase->done = true;
            }
        }

        if (rvrt_frame_is_complete(frame)) {
            phase->complete_count++;
            phase->done = true;
        }

#if RVRT_SESSION_IRQ_MAX_CYCLES > 0
        if (__RARELY((__get_rv_cycle() - start_cycles) >
                     (rv_counter_t)RVRT_SESSION_IRQ_MAX_CYCLES)) {
            phase->hardware_error = true;
            phase->done = true;
        }
#endif
    }

    __WMB();
    RESTORE_IRQ_CSR_CONTEXT();
}

rvrt_session_status_t rvrt_session_init(rvrt_session_t *session,
                                        const rvrt_session_config_t *config)
{
    if (__RARELY((session == NULL) || (config == NULL) ||
                 (config->artifact == NULL) || (config->rx_frames == NULL) ||
                 (config->output == NULL) || (config->rx_capacity == 0U) ||
                 (config->output_size == 0U))) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    uint32_t output_frames = 0U;
    const rvrt_artifact_status_t artifact_status =
        rvrt_artifact_output_entry_count(config->artifact, config->thread_index,
                                         config->output_index, &output_frames);
    if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    if (__RARELY(config->rx_capacity < required_rx_capacity(output_frames))) {
        return RVRT_SESSION_STATUS_BUFFER_TOO_SMALL;
    }

    session->artifact = config->artifact;
    session->thread_index = config->thread_index;
    session->input_index = config->input_index;
    session->output_index = config->output_index;
    session->rx_frames = config->rx_frames;
    session->rx_capacity = config->rx_capacity;
    session->output = config->output;
    session->output_size = config->output_size;
    session->output_entry_count = output_frames;
#if RVRT_SESSION_ENABLE_STATS
    clear_stats(&session->stats);
#endif
    clear_phase(&session->phase, 0U);
    return register_irq(session);
}

rvrt_session_status_t rvrt_session_start(rvrt_session_t *session,
                                         uint32_t timeout_ms)
{
    if (__RARELY(session == NULL)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    rvrt_frame_t init_frame = {0};
    const rvrt_status_t runtime_status = rvrt_build_init_frame(
        session->artifact, session->thread_index, &init_frame);
    if (__RARELY(runtime_status != RVRT_STATUS_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    arm_phase(session, 0U);
    noc_fifo_write_frame_words(init_frame.high, init_frame.low);

    rv_counter_t cycles = 0U;
    rvrt_session_status_t status = wait_phase(session, timeout_ms, &cycles);
    collect_phase_stats(session);
#if RVRT_SESSION_ENABLE_STATS
    session->stats.init_wait_cycles += cycles;
#else
    (void)cycles;
#endif
    return status;
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
    if (__RARELY(session->rx_capacity <
                 required_rx_capacity(session->output_entry_count))) {
        return RVRT_SESSION_STATUS_BUFFER_TOO_SMALL;
    }

    rvrt_frame_t sync_frame = {0};
    rvrt_status_t runtime_status = rvrt_build_sync_frame(
        session->artifact, session->thread_index, &sync_frame);
    if (__RARELY(runtime_status != RVRT_STATUS_OK)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
    const uint32_t sync_count = (sync_frame.low == 0U) ? 1U : sync_frame.low;
    sync_frame.low = 1U;

    memset(session->output, 0, session->output_size);

    rvrt_input_cursor_t cursor = {0};
    rvrt_input_cursor_init(&cursor, session->thread_index, session->input_index,
                           0U);

    const rv_counter_t start_cycles = __get_rv_cycle();
    while (true) {
        uint32_t chunk_count = 0U;
        runtime_status = rvrt_encode_input_chunk(
            session->artifact, &cursor, input, input_size, workspace,
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
            noc_fifo_write_frame_words(workspace[i].high, workspace[i].low);
#if RVRT_SESSION_ENABLE_STATS
            session->stats.sent_input_frames++;
#endif
        }

        if (runtime_status == RVRT_STATUS_DONE) {
            break;
        }
    }

    for (uint32_t sync_index = 0U; sync_index < sync_count; ++sync_index) {
        arm_phase(session, session->output_entry_count);
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

        status = decode_phase(session);
        if (__RARELY(status != RVRT_SESSION_STATUS_OK)) {
            return status;
        }
#if RVRT_SESSION_ENABLE_STATS
        session->stats.sync_phases++;
#endif
    }

#if RVRT_SESSION_ENABLE_STATS
    session->stats.total_cycles = __get_rv_cycle() - start_cycles;
#else
    (void)start_cycles;
#endif
    return RVRT_SESSION_STATUS_OK;
}

rvrt_session_status_t rvrt_session_get_stats(const rvrt_session_t *session,
                                             rvrt_session_stats_t *stats)
{
    if (__RARELY(stats == NULL)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }

    clear_stats(stats);
#if RVRT_SESSION_ENABLE_STATS
    if (__RARELY(session == NULL)) {
        return RVRT_SESSION_STATUS_RUNTIME_ERROR;
    }
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
