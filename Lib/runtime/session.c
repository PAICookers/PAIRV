#include "session_internal.h"

#include <stddef.h>
#include <string.h>

#include "debug.h"
#include "evalsoc_noc.h"

static rvrt_session_t *g_active_session;

void paicore_noc_handler(void);

/**
 * @brief Register the NoC ISR and make session the sole active receiver.
 *
 * The ISR dispatches through g_active_session, so ownership is exclusive until
 * rvrt_session_deinit() detaches it.
 */
static rvrt_session_status_t register_irq(rvrt_session_t *session)
{
    if (g_active_session != NULL) {
        return RVRT_SESSION_BUSY;
    }
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

static void clear_rx_barrier(rvrt_session_rx_barrier_t *rx_barrier)
{
    rx_barrier->active = false;
    rx_barrier->completed = false;
    rx_barrier->overflow = false;
    rx_barrier->hardware_error = false;
    rx_barrier->rx_count = 0U;
#if RVRT_SESSION_ENABLE_STATS
    rx_barrier->received_count = 0U;
    rx_barrier->output_work_count = 0U;
    rx_barrier->complete_count = 0U;
#endif
    rx_barrier->rx_frame_handler = NULL;
    rx_barrier->rx_frame_handler_user_data = NULL;
    rx_barrier->rx_frame_handler_status = RVRT_SESSION_OK;
}

static void clear_sync_epoch(rvrt_session_t *session)
{
    session->sync_mode = RVRT_SESSION_SYNC_MODE_UNSET;
    session->completed_timesteps = 0U;
}

/**
 * @brief Start an IRQ receive barrier before a control frame is sent.
 *
 * The write barrier makes the cleared barrier state visible before active
 * allows the ISR to append received frames.
 */
static void start_rx_barrier(rvrt_session_t *session,
                             rvrt_session_rx_frame_handler_t rx_frame_handler,
                             void *rx_frame_handler_user_data)
{
    rvrt_session_rx_barrier_t *const rx_barrier = &session->rx_barrier;

    noc_irq_disable();
    noc_irq_ack();
    clear_rx_barrier(rx_barrier);
    rx_barrier->rx_frame_handler = rx_frame_handler;
    rx_barrier->rx_frame_handler_user_data = rx_frame_handler_user_data;
    __WMB();
    rx_barrier->active = true;
    noc_irq_enable();
}

static void receive_rx_barrier_frame(rvrt_session_t *session,
                                     const rvrt_frame_t *frame)
{
    rvrt_session_rx_barrier_t *const rx_barrier = &session->rx_barrier;
#if RVRT_SESSION_ENABLE_STATS
    rx_barrier->received_count++;
    if (rvrt_frame_is_work(frame)) {
        rx_barrier->output_work_count++;
    }
    const bool is_complete = rvrt_frame_is_complete(frame);
    if (is_complete) {
        rx_barrier->complete_count++;
    }
#else
    const bool is_complete = rvrt_frame_is_complete(frame);
#endif

    if ((rx_barrier->rx_frame_handler != NULL) && !is_complete) {
        if (rx_barrier->rx_frame_handler_status == RVRT_SESSION_OK) {
            rx_barrier->rx_frame_handler_status = rx_barrier->rx_frame_handler(
                rx_barrier->rx_frame_handler_user_data, frame);
        }
        return;
    }

    if (rx_barrier->rx_frame_handler == NULL) {
        const uint32_t index = rx_barrier->rx_count;
        if (__RARELY(index >= session->rx_capacity)) {
            rx_barrier->overflow = true;
            rx_barrier->hardware_error = true;
            __WMB();
            rx_barrier->completed = true;
            return;
        }
        session->rx_frames[index] = *frame;
        rx_barrier->rx_count = index + 1U;
    }
    if (is_complete) {
        __WMB();
        rx_barrier->completed = true;
    }
}

/**
 * @brief Wait for the ISR to finish the active control RX barrier.
 *
 * On timeout this function disables NoC IRQ delivery and marks the barrier as
 * a hardware error, so callers must start a fresh synchronization barrier.
 */
static rvrt_session_status_t wait_rx_barrier(rvrt_session_t *session,
                                             uint32_t timeout_ms,
                                             rv_counter_t *cycles_out)
{
    rvrt_session_rx_barrier_t *const rx_barrier = &session->rx_barrier;
    const rv_counter_t start_cycles = __get_rv_cycle();
    const rv_counter_t limit_cycles =
        ((rv_counter_t)(SystemCoreClock / 1000U)) * timeout_ms;

    while (__USUALLY(!rx_barrier->completed)) {
        if (__RARELY((__get_rv_cycle() - start_cycles) > limit_cycles)) {
            noc_irq_disable();
            __RMB();
            if (rx_barrier->completed) {
                break;
            }
#if RVRT_SESSION_ENABLE_STATS
            RV_DEBUG_LOGE(
                "runtime",
                "RX barrier timeout pending=%u enabled=%u received=%u "
                "work=%u complete=%u",
                (unsigned)noc_irq_pending(), (unsigned)noc_irq_is_enabled(),
                (unsigned)rx_barrier->received_count,
                (unsigned)rx_barrier->output_work_count,
                (unsigned)rx_barrier->complete_count);
#else
            RV_DEBUG_LOGE("runtime", "RX barrier timeout pending=%u enabled=%u",
                          (unsigned)noc_irq_pending(),
                          (unsigned)noc_irq_is_enabled());
#endif
            rx_barrier->active = false;
            rx_barrier->hardware_error = true;
            return RVRT_SESSION_TIMEOUT;
        }
    }

    noc_irq_disable();
    __RMB();
    rx_barrier->active = false;
    if (cycles_out != NULL) {
        *cycles_out = __get_rv_cycle() - start_cycles;
    }
    if (__RARELY(rx_barrier->overflow)) {
        return RVRT_SESSION_OVERFLOW;
    }
    if (__RARELY(rx_barrier->hardware_error)) {
        return RVRT_SESSION_HARDWARE_ERROR;
    }
    if (__RARELY(rx_barrier->rx_frame_handler_status != RVRT_SESSION_OK)) {
        return rx_barrier->rx_frame_handler_status;
    }
    return RVRT_SESSION_OK;
}

/**
 * @brief Send one control frame through the common completion barrier.
 *
 * A NULL output pair intentionally discards the barrier response, which is used
 * for model reset where the only expected response is the completion frame.
 */
static rvrt_session_status_t
run_control_barrier(rvrt_session_t *session, const rvrt_frame_t *control_frame,
                    uint32_t timeout_ms, bool record_sync_wait,
                    const rvrt_frame_t **rx_frames, uint32_t *rx_frame_count,
                    rvrt_session_rx_frame_handler_t rx_frame_handler,
                    void *rx_frame_handler_user_data)
{
    start_rx_barrier(session, rx_frame_handler, rx_frame_handler_user_data);
    noc_fifo_write_frame_words(control_frame->high, control_frame->low);
#if RVRT_SESSION_ENABLE_STATS
    rv_counter_t barrier_cycles = 0U;
    const rvrt_session_status_t status =
        wait_rx_barrier(session, timeout_ms, &barrier_cycles);
    if (record_sync_wait) {
        session->stats.sync_wait_cycles += barrier_cycles;
    }
#else
    (void)record_sync_wait;
    const rvrt_session_status_t status =
        wait_rx_barrier(session, timeout_ms, NULL);
#endif
    if (rx_frames != NULL) {
        *rx_frames = session->rx_frames;
    }
    if (rx_frame_count != NULL) {
        *rx_frame_count = session->rx_barrier.rx_count;
    }
#if RVRT_SESSION_ENABLE_STATS
    session->stats.sent_frames++;
    session->stats.rx_frames += session->rx_barrier.received_count;
    session->stats.output_work_frames += session->rx_barrier.output_work_count;
    session->stats.complete_frames += session->rx_barrier.complete_count;
    session->stats.overflow |= session->rx_barrier.overflow;
    session->stats.hardware_error |= session->rx_barrier.hardware_error;
#endif
    if (status != RVRT_SESSION_OK) {
        noc_irq_disable();
        session->rx_barrier.active = false;
        session->rx_barrier.rx_frame_handler = NULL;
        session->rx_barrier.rx_frame_handler_user_data = NULL;
        session->faulted = true;
    }
    return status;
}

static rvrt_session_status_t discard_reset_frame(void *user_data,
                                                 const rvrt_frame_t *frame)
{
    (void)user_data;
    (void)frame;
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
    if (g_active_session != NULL) {
        return RVRT_SESSION_BUSY;
    }

    rvrt_artifact_runtime_t runtime = {0};
    if (__RARELY(rvrt_artifact_thread_runtime(config->artifact,
                                              config->thread_index,
                                              &runtime) != RVRT_ARTIFACT_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    memset(session, 0, sizeof(*session));
    session->artifact = config->artifact;
    session->thread_index = config->thread_index;
    session->rx_frames = config->rx_frames;
    session->rx_capacity = config->rx_capacity;
    session->faulted = false;
    clear_sync_epoch(session);
    session->stats.enabled = RVRT_SESSION_ENABLE_STATS != 0;
    clear_rx_barrier(&session->rx_barrier);
    const rvrt_session_status_t status = register_irq(session);
    if (status != RVRT_SESSION_OK) {
        memset(session, 0, sizeof(*session));
    }
    return status;
}

rvrt_session_status_t rvrt_session_deinit(rvrt_session_t *session)
{
    if (session == NULL) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if ((g_active_session != NULL) && (g_active_session != session)) {
        return RVRT_SESSION_BUSY;
    }
    if ((g_active_session == session) && session->rx_barrier.active) {
        return RVRT_SESSION_BUSY;
    }

    noc_irq_disable();
    noc_irq_ack();
    if (g_active_session == session) {
        g_active_session = NULL;
    }
    memset(session, 0, sizeof(*session));
    return RVRT_SESSION_OK;
}

rvrt_session_status_t rvrt_session_load_config(rvrt_session_t *session)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (__RARELY(session->faulted)) {
        return RVRT_SESSION_FAULTED;
    }
    if (__RARELY(session->rx_barrier.active)) {
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
                 ((frame_count != 0U) && (frames == NULL)))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (__RARELY(session->faulted)) {
        return RVRT_SESSION_FAULTED;
    }
    if (__RARELY(session->rx_barrier.active)) {
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

rvrt_session_status_t rvrt_session_reset_model(rvrt_session_t *session,
                                               uint32_t timeout_ms)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL) ||
                 (session->rx_frames == NULL) ||
                 (session->rx_capacity == 0U))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (__RARELY(session->faulted)) {
        return RVRT_SESSION_FAULTED;
    }
    if (__RARELY(session->rx_barrier.active)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    rvrt_frame_t init_frame = {0};
    if (__RARELY(rvrt_build_init_frame(session->artifact, session->thread_index,
                                       &init_frame) != RVRT_CODEC_STATUS_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    const rvrt_session_status_t status =
        run_control_barrier(session, &init_frame, timeout_ms, false, NULL, NULL,
                            discard_reset_frame, NULL);
    if (status == RVRT_SESSION_OK) {
        clear_sync_epoch(session);
    }
    return status;
}

static rvrt_session_status_t
sync_wait_payload_impl(rvrt_session_t *session, uint32_t sync_payload,
                       uint32_t timeout_ms, const rvrt_frame_t **rx_frames,
                       uint32_t *rx_frame_count,
                       rvrt_session_rx_frame_handler_t rx_frame_handler,
                       void *rx_frame_handler_user_data)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL) ||
                 (session->rx_frames == NULL) || (session->rx_capacity == 0U) ||
                 (((rx_frames == NULL) || (rx_frame_count == NULL)) &&
                  (rx_frame_handler == NULL)))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (__RARELY(session->faulted)) {
        return RVRT_SESSION_FAULTED;
    }
    if (__RARELY(session->rx_barrier.active)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    if (rx_frames != NULL) {
        *rx_frames = NULL;
    }
    if (rx_frame_count != NULL) {
        *rx_frame_count = 0U;
    }
    rvrt_frame_t sync_frame = {0};
    if (__RARELY(rvrt_build_sync_payload_frame(
                     session->artifact, session->thread_index, sync_payload,
                     &sync_frame) != RVRT_CODEC_STATUS_OK)) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

    const rvrt_session_status_t status = run_control_barrier(
        session, &sync_frame, timeout_ms, true, rx_frames, rx_frame_count,
        rx_frame_handler, rx_frame_handler_user_data);
#if RVRT_SESSION_ENABLE_STATS
    if (status == RVRT_SESSION_OK) {
        session->stats.sync_barriers++;
    }
#endif
    return status;
}

rvrt_session_status_t rvrt_session_sync_wait_payload(
    rvrt_session_t *session, uint32_t sync_payload, uint32_t timeout_ms,
    const rvrt_frame_t **rx_frames, uint32_t *rx_frame_count)
{
    if (session == NULL) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (session->faulted) {
        return RVRT_SESSION_FAULTED;
    }
    if (session->sync_mode == RVRT_SESSION_SYNC_MODE_TIMELINE) {
        return RVRT_SESSION_SYNC_MODE_ERROR;
    }
    const rvrt_session_status_t status =
        sync_wait_payload_impl(session, sync_payload, timeout_ms, rx_frames,
                               rx_frame_count, NULL, NULL);
    if (status == RVRT_SESSION_OK) {
        session->sync_mode = RVRT_SESSION_SYNC_MODE_RAW_PAYLOAD;
    }
    return status;
}

static rvrt_session_status_t sync_wait_until_impl(
    rvrt_session_t *session, uint32_t completed_timesteps, uint32_t timeout_ms,
    const rvrt_frame_t **rx_frames, uint32_t *rx_frame_count,
    rvrt_session_rx_frame_handler_t rx_frame_handler, void *user_data)
{
    if (session == NULL) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    if (session->faulted) {
        return RVRT_SESSION_FAULTED;
    }
    if ((session->sync_mode == RVRT_SESSION_SYNC_MODE_RAW_PAYLOAD) ||
        ((session->sync_mode == RVRT_SESSION_SYNC_MODE_TIMELINE) &&
         (completed_timesteps <= session->completed_timesteps))) {
        return RVRT_SESSION_SYNC_MODE_ERROR;
    }

    const uint32_t sync_payload =
        (session->sync_mode == RVRT_SESSION_SYNC_MODE_TIMELINE)
            ? completed_timesteps - session->completed_timesteps
            : completed_timesteps;
    const rvrt_session_status_t status =
        sync_wait_payload_impl(session, sync_payload, timeout_ms, rx_frames,
                               rx_frame_count, rx_frame_handler, user_data);
    if (status == RVRT_SESSION_OK) {
        session->sync_mode = RVRT_SESSION_SYNC_MODE_TIMELINE;
        session->completed_timesteps = completed_timesteps;
    }
    return status;
}

rvrt_session_status_t rvrt_session_sync_wait_until(
    rvrt_session_t *session, uint32_t completed_timesteps, uint32_t timeout_ms,
    const rvrt_frame_t **rx_frames, uint32_t *rx_frame_count)
{
    return sync_wait_until_impl(session, completed_timesteps, timeout_ms,
                                rx_frames, rx_frame_count, NULL, NULL);
}

rvrt_session_status_t rvrt_session_sync_wait_until_with_rx_handler(
    rvrt_session_t *session, uint32_t completed_timesteps, uint32_t timeout_ms,
    rvrt_session_rx_frame_handler_t rx_frame_handler, void *user_data)
{
    if (rx_frame_handler == NULL) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }
    return sync_wait_until_impl(session, completed_timesteps, timeout_ms, NULL,
                                NULL, rx_frame_handler, user_data);
}

void paicore_noc_handler(void)
{
    SAVE_IRQ_CSR_CONTEXT();
    noc_irq_ack();
    noc_irq_disable();

    rvrt_session_t *const session = g_active_session;
    if (__RARELY((session == NULL) || !session->rx_barrier.active)) {
        RESTORE_IRQ_CSR_CONTEXT();
        return;
    }

    rvrt_session_rx_barrier_t *const rx_barrier = &session->rx_barrier;
    while (!rx_barrier->completed) {
        uint32_t high = 0U;
        uint32_t low = 0U;
        if (__RARELY(noc_fifo_read_frame_words(&high, &low) != 0)) {
            rx_barrier->hardware_error = true;
            __WMB();
            rx_barrier->completed = true;
        } else {
            const rvrt_frame_t frame = {high, low};
            receive_rx_barrier_frame(session, &frame);
        }
    }

    RESTORE_IRQ_CSR_CONTEXT();
}

rvrt_session_status_t rvrt_session_get_stats(const rvrt_session_t *session,
                                             rvrt_session_stats_t *stats)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL) ||
                 (stats == NULL))) {
        return RVRT_SESSION_RUNTIME_ERROR;
    }

#if RVRT_SESSION_ENABLE_STATS
    const rvrt_session_stats_t snapshot = session->stats;
    *stats = snapshot;
#else
    *stats = (rvrt_session_stats_t){0};
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
        case RVRT_SESSION_SYNC_MODE_ERROR:
            return "sync mode error";
        case RVRT_SESSION_SCHEDULE_UNSUPPORTED:
            return "schedule unsupported";
        case RVRT_SESSION_FAULTED:
            return "session faulted";
        case RVRT_SESSION_BUSY:
            return "session busy";
        default:
            return "unknown";
    }
}
