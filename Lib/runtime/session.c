#include "session.h"

#include <stddef.h>

#include "debug.h"
#include "evalsoc_noc.h"

static rvrt_session_t *g_active_session;

void paicore_noc_handler(void);

/**
 * @brief Register the NoC ISR and make session the sole active receiver.
 *
 * The ISR dispatches through g_active_session, so a second initialized session
 * replaces the previous receiver and is not a supported concurrent use case.
 */
static rvrt_session_status_t register_irq(rvrt_session_t *session)
{
    noc_irq_disable();
    g_active_session = session;
    const int32_t result =
        ECLIC_Register_IRQ(PAICORE_NOC_IRQn, ECLIC_NON_VECTOR_INTERRUPT,
                           ECLIC_LEVEL_TRIGGER, 1U, 0U, paicore_noc_handler);
    if (__RARELY(result != 0)) {
        g_active_session = NULL;
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    __enable_irq();
    return RVRT_SESSION_OK;
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
 * @brief Reset and publish an IRQ receive phase before a sync frame is sent.
 *
 * The write barrier makes the cleared phase state visible before armed allows
 * the ISR to append received frames.
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
 * @brief Wait for the ISR to finish the armed phase, then disarm it.
 *
 * On timeout this function disables NoC IRQ delivery and marks the phase as a
 * hardware error, so callers must start a fresh synchronization phase.
 */
static rvrt_session_status_t wait_phase(rvrt_session_t *session,
                                        uint32_t timeout_ms,
                                        rv_counter_t *cycles_out)
{
    rvrt_session_phase_t *const phase = &session->phase;
    const rv_counter_t start_cycles = __get_rv_cycle();
    const rv_counter_t limit_cycles =
        ((rv_counter_t)(SystemCoreClock / 1000U)) * timeout_ms;

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
            return RVRT_SESSION_TIMEOUT;
        }
    }

    noc_irq_disable();
    __RMB();
    phase->armed = false;
    if (cycles_out != NULL) {
        *cycles_out = __get_rv_cycle() - start_cycles;
    }
    if (__RARELY(phase->overflow)) {
        return RVRT_SESSION_OVERFLOW;
    }
    if (__RARELY(phase->hardware_error)) {
        return RVRT_SESSION_HARDWARE_ERROR;
    }
    return RVRT_SESSION_OK;
}

rvrt_session_status_t rvrt_session_init(rvrt_session_t *session,
                                        const rvrt_session_config_t *config)
{
    if (__RARELY((session == NULL) || (config == NULL) ||
                 (config->artifact == NULL) || (config->rx_frames == NULL) ||
                 (config->rx_capacity == 0U))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    rvrt_artifact_runtime_t runtime = {0};
    if (__RARELY(rvrt_artifact_thread_runtime(config->artifact,
                                              config->thread_index,
                                              &runtime) != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    session->artifact = config->artifact;
    session->thread_index = config->thread_index;
    session->rx_frames = config->rx_frames;
    session->rx_capacity = config->rx_capacity;
#if RVRT_SESSION_ENABLE_STATS
    session->stats = (rvrt_session_stats_t){0};
#endif
    clear_phase(&session->phase);
    return register_irq(session);
}

rvrt_session_status_t rvrt_session_load_config(rvrt_session_t *session)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL) ||
                 session->phase.armed)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    uint32_t word_count = 0U;
    rvrt_artifact_status_t artifact_status =
        rvrt_artifact_config_word_count(session->artifact, &word_count);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) || (word_count == 0U) ||
                 ((word_count % 2U) != 0U))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    const bool irq_was_enabled = noc_irq_is_enabled();
    noc_irq_disable();
    const uint32_t frame_count = word_count / 2U;
    for (uint32_t i = 0U; i < frame_count; ++i) {
        uint32_t high = 0U;
        uint32_t low = 0U;
        artifact_status =
            rvrt_artifact_config_frame_words(session->artifact, i, &high, &low);
        if (__RARELY(artifact_status != RVRT_ARTIFACT_OK)) {
            if (irq_was_enabled) {
                noc_irq_enable();
            }
            return RVRT_SESSION_RUNTIME_ERROR;
        }
        noc_fifo_write_frame_words_unlocked(high, low);
    }
    if (irq_was_enabled) {
        noc_irq_enable();
    }

#if RVRT_SESSION_ENABLE_STATS
    session->stats.sent_frames += frame_count;
#endif
    return RVRT_SESSION_OK;
}

rvrt_session_status_t rvrt_session_send_frames(rvrt_session_t *session,
                                               const rvrt_frame_t *frames,
                                               uint32_t frame_count)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL) ||
                 ((frame_count != 0U) && (frames == NULL)) ||
                 session->phase.armed)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    const bool irq_was_enabled = noc_irq_is_enabled();
    noc_irq_disable();
    for (uint32_t i = 0U; i < frame_count; ++i) {
        noc_fifo_write_frame_words_unlocked(frames[i].high, frames[i].low);
    }
    if (irq_was_enabled) {
        noc_irq_enable();
    }

#if RVRT_SESSION_ENABLE_STATS
    session->stats.sent_frames += frame_count;
#endif
    return RVRT_SESSION_OK;
}

rvrt_session_status_t rvrt_session_sync_wait(rvrt_session_t *session,
                                             uint32_t sync_steps,
                                             uint32_t timeout_ms,
                                             const rvrt_frame_t **rx_frames,
                                             uint32_t *rx_frame_count)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL) ||
                 (session->rx_frames == NULL) || (session->rx_capacity == 0U) ||
                 (rx_frames == NULL) || (rx_frame_count == NULL) ||
                 session->phase.armed)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    *rx_frames = NULL;
    *rx_frame_count = 0U;
    rvrt_frame_t sync_frame = {0};
    if (__RARELY(rvrt_build_sync_frame(session->artifact, session->thread_index,
                                       sync_steps,
                                       &sync_frame) != RVRT_STATUS_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    arm_phase(session);
    noc_fifo_write_frame_words(sync_frame.high, sync_frame.low);
#if RVRT_SESSION_ENABLE_STATS
    session->stats.sent_frames++;
    rv_counter_t phase_cycles = 0U;
    const rvrt_session_status_t status =
        wait_phase(session, timeout_ms, &phase_cycles);
    session->stats.sync_wait_cycles += phase_cycles;
#else
    const rvrt_session_status_t status = wait_phase(session, timeout_ms, NULL);
#endif
    *rx_frames = session->rx_frames;
    *rx_frame_count = session->phase.rx_count;
#if RVRT_SESSION_ENABLE_STATS
    session->stats.rx_frames += session->phase.rx_count;
    session->stats.output_work_frames += session->phase.output_work_count;
    session->stats.complete_frames += session->phase.complete_count;
    session->stats.overflow |= session->phase.overflow;
    session->stats.hardware_error |= session->phase.hardware_error;
    if (status == RVRT_SESSION_OK) {
        session->stats.sync_phases++;
    }
#endif
    return status;
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
        if (__RARELY(noc_fifo_read_frame_words(&high, &low) != 0)) {
            phase->hardware_error = true;
            __WMB();
            phase->done = true;
            break;
        }
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

rvrt_session_status_t rvrt_session_get_stats(const rvrt_session_t *session,
                                             rvrt_session_stats_t *stats)
{
    if (__RARELY((session == NULL) || (stats == NULL))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    *stats = (rvrt_session_stats_t){0};
#if RVRT_SESSION_ENABLE_STATS
    *stats = session->stats;
#endif
    return RVRT_SESSION_OK;
}

const char *rvrt_session_status_string(rvrt_session_status_t status)
{
    switch (status) {
        case RVRT_SESSION_OK:
            return "ok";
        case RVRT_SESSION_TIMEOUT:
            return "timeout";
        case RVRT_SESSION_BUFFER_TOO_SMALL:
            return "buffer too small";
        case RVRT_SESSION_OVERFLOW:
            return "overflow";
        case RVRT_SESSION_HARDWARE_ERROR:
            return "hardware error";
        case RVRT_SESSION_RUNTIME_ERROR:
            return "runtime error";
        default:
            return "unknown";
    }
}
