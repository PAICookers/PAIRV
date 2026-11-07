#include "transport_thread_internal.h"

#include <stddef.h>
#include <string.h>

#include "debug.h"
#include "evalsoc_noc.h"

static rvrt_transport_t *g_active_session;

void paicore_noc_handler(void);

/**
 * @brief Register the NoC ISR and make session the sole active receiver.
 *
 * The ISR dispatches through g_active_session, so ownership is exclusive until
 * rvrt_transport_deinit() detaches it.
 */
static rvrt_runtime_status_t register_irq(rvrt_transport_t *session)
{
    if (g_active_session != NULL) {
        return RVRT_RUNTIME_BUSY;
    }
    noc_irq_disable();
    g_active_session = session;
    const int32_t result =
        ECLIC_Register_IRQ(PAICORE_NOC_IRQn, ECLIC_NON_VECTOR_INTERRUPT,
                           ECLIC_LEVEL_TRIGGER, 1U, 0U, paicore_noc_handler);
    if (__RARELY(result != 0)) {
        g_active_session = NULL;
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    __enable_irq();
    return RVRT_RUNTIME_OK;
}

static void clear_rx_barrier(rvrt_runtime_rx_barrier_t *rx_barrier)
{
    rx_barrier->active = false;
    rx_barrier->completed = false;
    rx_barrier->overflow = false;
    rx_barrier->hardware_error = false;
    rx_barrier->rx_count = 0U;
#if RVRT_ENABLE_STATS
    rx_barrier->received_count = 0U;
    rx_barrier->output_work_count = 0U;
    rx_barrier->complete_count = 0U;
#endif
    rx_barrier->rx_frame_handler = NULL;
    rx_barrier->rx_frame_handler_user_data = NULL;
    rx_barrier->rx_frame_handler_status = RVRT_RUNTIME_OK;
}

/**
 * @brief Start an IRQ receive barrier before a control frame is sent.
 *
 * The write barrier makes the cleared barrier state visible before active
 * allows the ISR to append received frames.
 */
static void start_rx_barrier(rvrt_transport_t *session,
                             rvrt_runtime_rx_frame_handler_t rx_frame_handler,
                             void *rx_frame_handler_user_data)
{
    rvrt_runtime_rx_barrier_t *const rx_barrier = &session->rx_barrier;

    noc_irq_disable();
    noc_irq_ack();
    clear_rx_barrier(rx_barrier);
    rx_barrier->rx_frame_handler = rx_frame_handler;
    rx_barrier->rx_frame_handler_user_data = rx_frame_handler_user_data;
    __WMB();
    rx_barrier->active = true;
    noc_irq_enable();
}

static void receive_rx_barrier_frame(rvrt_transport_t *session,
                                     const rvrt_frame_t *frame)
{
    rvrt_runtime_rx_barrier_t *const rx_barrier = &session->rx_barrier;
#if RVRT_ENABLE_STATS
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
        if (rx_barrier->rx_frame_handler_status == RVRT_RUNTIME_OK) {
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
static rvrt_runtime_status_t wait_rx_barrier(rvrt_transport_t *session,
                                             uint32_t timeout_ms,
                                             rv_counter_t *cycles_out)
{
    rvrt_runtime_rx_barrier_t *const rx_barrier = &session->rx_barrier;
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
#if RVRT_ENABLE_STATS
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
            return RVRT_RUNTIME_TIMEOUT;
        }
    }

    noc_irq_disable();
    __RMB();
    rx_barrier->active = false;
    if (cycles_out != NULL) {
        *cycles_out = __get_rv_cycle() - start_cycles;
    }
    if (__RARELY(rx_barrier->overflow)) {
        return RVRT_RUNTIME_OVERFLOW;
    }
    if (__RARELY(rx_barrier->hardware_error)) {
        return RVRT_RUNTIME_HARDWARE_ERROR;
    }
    if (__RARELY(rx_barrier->rx_frame_handler_status != RVRT_RUNTIME_OK)) {
        return rx_barrier->rx_frame_handler_status;
    }
    return RVRT_RUNTIME_OK;
}

/**
 * @brief Send one control frame through the common completion barrier.
 *
 * A NULL output pair intentionally discards the barrier response, which is used
 * for model reset where the only expected response is the completion frame.
 */
static rvrt_runtime_status_t run_control_barrier(
    rvrt_transport_t *session, const rvrt_frame_t *control_frame,
    uint32_t timeout_ms, bool is_sync_barrier, const rvrt_frame_t **rx_frames,
    uint32_t *rx_frame_count, rvrt_runtime_rx_frame_handler_t rx_frame_handler,
    void *rx_frame_handler_user_data)
{
    start_rx_barrier(session, rx_frame_handler, rx_frame_handler_user_data);
    noc_fifo_write_frame_words(control_frame->high, control_frame->low);
#if RVRT_ENABLE_STATS
    rv_counter_t barrier_cycles = 0U;
    const rvrt_runtime_status_t status =
        wait_rx_barrier(session, timeout_ms, &barrier_cycles);
    if (is_sync_barrier) {
        session->stats.sync_wait_cycles += barrier_cycles;
    } else {
        session->stats.init_wait_cycles += barrier_cycles;
    }
#else
    (void)is_sync_barrier;
    const rvrt_runtime_status_t status =
        wait_rx_barrier(session, timeout_ms, NULL);
#endif
    if (rx_frames != NULL) {
        *rx_frames = session->rx_frames;
    }
    if (rx_frame_count != NULL) {
        *rx_frame_count = session->rx_barrier.rx_count;
    }
#if RVRT_ENABLE_STATS
    session->stats.sent_frames++;
    session->stats.rx_frames += session->rx_barrier.received_count;
    session->stats.output_work_frames += session->rx_barrier.output_work_count;
    session->stats.complete_frames += session->rx_barrier.complete_count;
    session->stats.overflow |= session->rx_barrier.overflow;
    session->stats.hardware_error |= session->rx_barrier.hardware_error;
#endif
    if (status != RVRT_RUNTIME_OK) {
        noc_irq_disable();
        session->rx_barrier.active = false;
        session->rx_barrier.rx_frame_handler = NULL;
        session->rx_barrier.rx_frame_handler_user_data = NULL;
        session->faulted = true;
    }
    return status;
}

static rvrt_runtime_status_t discard_reset_frame(void *user_data,
                                                 const rvrt_frame_t *frame)
{
    (void)user_data;
    (void)frame;
    return RVRT_RUNTIME_OK;
}

rvrt_runtime_status_t rvrt_transport_init(rvrt_transport_t *session,
                                          const rvrt_transport_config_t *config)
{
    if (__RARELY((session == NULL) || (config == NULL) ||
                 (config->artifact == NULL) || (config->rx_frames == NULL) ||
                 (config->rx_capacity == 0U))) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (g_active_session != NULL) {
        return RVRT_RUNTIME_BUSY;
    }

    memset(session, 0, sizeof(*session));
    session->artifact = config->artifact;
    session->rx_frames = config->rx_frames;
    session->rx_capacity = config->rx_capacity;
    session->faulted = false;
#if RVRT_ENABLE_STATS
    session->stats.enabled = true;
#endif
    clear_rx_barrier(&session->rx_barrier);
    const rvrt_runtime_status_t status = register_irq(session);
    if (status != RVRT_RUNTIME_OK) {
        memset(session, 0, sizeof(*session));
    }
    return status;
}

rvrt_runtime_status_t rvrt_transport_deinit(rvrt_transport_t *session)
{
    if (session == NULL) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if ((g_active_session != NULL) && (g_active_session != session)) {
        return RVRT_RUNTIME_BUSY;
    }
    if ((g_active_session == session) && session->rx_barrier.active) {
        return RVRT_RUNTIME_BUSY;
    }

    noc_irq_disable();
    noc_irq_ack();
    if (g_active_session == session) {
        g_active_session = NULL;
    }
    memset(session, 0, sizeof(*session));
    return RVRT_RUNTIME_OK;
}

rvrt_runtime_status_t rvrt_transport_load_config(rvrt_transport_t *session)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL))) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (__RARELY(session->faulted)) {
        return RVRT_RUNTIME_FAULTED;
    }
    if (__RARELY(session->rx_barrier.active)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    uint32_t word_count = 0U;
    rvrt_artifact_status_t artifact_status =
        rvrt_artifact_config_word_count(session->artifact, &word_count);
    if (__RARELY((artifact_status != RVRT_ARTIFACT_OK) || (word_count == 0U) ||
                 ((word_count % 2U) != 0U))) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    const bool irq_was_enabled = noc_irq_is_enabled();
#if RVRT_ENABLE_STATS
    const rv_counter_t config_submit_start = __get_rv_cycle();
#endif
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
            return RVRT_RUNTIME_RUNTIME_ERROR;
        }
        noc_fifo_write_frame_words_unlocked(high, low);
    }
    if (irq_was_enabled) {
        noc_irq_enable();
    }
#if RVRT_ENABLE_STATS
    session->stats.sent_frames += frame_count;
    session->stats.config_frames += frame_count;
    session->stats.config_submit_cycles +=
        __get_rv_cycle() - config_submit_start;
#endif
    return RVRT_RUNTIME_OK;
}

rvrt_runtime_status_t rvrt_transport_send_frames(rvrt_transport_t *session,
                                                 const rvrt_frame_t *frames,
                                                 uint32_t frame_count)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL) ||
                 ((frame_count != 0U) && (frames == NULL)))) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (__RARELY(session->faulted)) {
        return RVRT_RUNTIME_FAULTED;
    }
    if (__RARELY(session->rx_barrier.active)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    const bool irq_was_enabled = noc_irq_is_enabled();
    noc_irq_disable();
    for (uint32_t i = 0U; i < frame_count; ++i) {
        noc_fifo_write_frame_words_unlocked(frames[i].high, frames[i].low);
    }
    if (irq_was_enabled) {
        noc_irq_enable();
    }

#if RVRT_ENABLE_STATS
    session->stats.sent_frames += frame_count;
#endif
    return RVRT_RUNTIME_OK;
}

static rvrt_runtime_status_t
sync_wait_payload_impl(rvrt_transport_t *session, uint32_t thread_index,
                       uint32_t sync_payload, uint32_t timeout_ms,
                       const rvrt_frame_t **rx_frames, uint32_t *rx_frame_count,
                       rvrt_runtime_rx_frame_handler_t rx_frame_handler,
                       void *rx_frame_handler_user_data)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL) ||
                 (session->rx_frames == NULL) || (session->rx_capacity == 0U) ||
                 (((rx_frames == NULL) || (rx_frame_count == NULL)) &&
                  (rx_frame_handler == NULL)))) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (__RARELY(session->faulted)) {
        return RVRT_RUNTIME_FAULTED;
    }
    if (__RARELY(session->rx_barrier.active)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    if (rx_frames != NULL) {
        *rx_frames = NULL;
    }
    if (rx_frame_count != NULL) {
        *rx_frame_count = 0U;
    }
    rvrt_frame_t sync_frame = {0};
    if (__RARELY(rvrt_build_sync_payload_frame(session->artifact, thread_index,
                                               sync_payload, &sync_frame) !=
                 RVRT_CODEC_STATUS_OK)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    const rvrt_runtime_status_t status = run_control_barrier(
        session, &sync_frame, timeout_ms, true, rx_frames, rx_frame_count,
        rx_frame_handler, rx_frame_handler_user_data);
#if RVRT_ENABLE_STATS
    if (status == RVRT_RUNTIME_OK) {
        session->stats.sync_barriers++;
    }
#endif
    return status;
}

rvrt_runtime_status_t rvrt_transport_sync_wait_payload_for_thread(
    rvrt_transport_t *session, uint32_t thread_index,
    rvrt_runtime_sync_mode_t *sync_mode, uint32_t sync_payload,
    uint32_t timeout_ms, const rvrt_frame_t **rx_frames,
    uint32_t *rx_frame_count)
{
    if ((session == NULL) || (sync_mode == NULL)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (session->faulted) {
        return RVRT_RUNTIME_FAULTED;
    }
    if (*sync_mode == RVRT_RUNTIME_SYNC_MODE_TIMELINE) {
        return RVRT_RUNTIME_SYNC_MODE_ERROR;
    }
    const rvrt_runtime_status_t status =
        sync_wait_payload_impl(session, thread_index, sync_payload, timeout_ms,
                               rx_frames, rx_frame_count, NULL, NULL);
    if (status == RVRT_RUNTIME_OK) {
        *sync_mode = RVRT_RUNTIME_SYNC_MODE_RAW_PAYLOAD;
    }
    return status;
}

rvrt_runtime_status_t rvrt_transport_reset_model_for_thread(
    rvrt_transport_t *session, uint32_t thread_index, uint32_t timeout_ms,
    rvrt_runtime_sync_mode_t *sync_mode, uint32_t *completed_timesteps)
{
    if ((session == NULL) || (session->artifact == NULL) ||
        (session->rx_frames == NULL) || (session->rx_capacity == 0U) ||
        (sync_mode == NULL) || (completed_timesteps == NULL)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (session->faulted || session->rx_barrier.active) {
        return session->faulted ? RVRT_RUNTIME_FAULTED
                                : RVRT_RUNTIME_RUNTIME_ERROR;
    }

    rvrt_frame_t init_frame = {0};
    if (rvrt_build_init_frame(session->artifact, thread_index, &init_frame) !=
        RVRT_CODEC_STATUS_OK) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    const rvrt_runtime_status_t status =
        run_control_barrier(session, &init_frame, timeout_ms, false, NULL, NULL,
                            discard_reset_frame, NULL);
    if (status == RVRT_RUNTIME_OK) {
        *sync_mode = RVRT_RUNTIME_SYNC_MODE_UNSET;
        *completed_timesteps = 0U;
    }
    return status;
}

rvrt_runtime_status_t rvrt_transport_sync_wait_until_for_thread(
    rvrt_transport_t *session, uint32_t thread_index,
    uint32_t completed_timesteps, uint32_t timeout_ms,
    rvrt_runtime_sync_mode_t *sync_mode, uint32_t *previous_completed_timesteps,
    const rvrt_frame_t **rx_frames, uint32_t *rx_frame_count,
    rvrt_runtime_rx_frame_handler_t rx_frame_handler, void *user_data)
{
    if ((session == NULL) || (sync_mode == NULL) ||
        (previous_completed_timesteps == NULL)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (session->faulted) {
        return RVRT_RUNTIME_FAULTED;
    }
    if ((session->rx_barrier.active) ||
        (*sync_mode == RVRT_RUNTIME_SYNC_MODE_RAW_PAYLOAD) ||
        ((*sync_mode == RVRT_RUNTIME_SYNC_MODE_TIMELINE) &&
         (completed_timesteps <= *previous_completed_timesteps))) {
        return session->rx_barrier.active ? RVRT_RUNTIME_RUNTIME_ERROR
                                          : RVRT_RUNTIME_SYNC_MODE_ERROR;
    }

    const uint32_t sync_payload =
        (*sync_mode == RVRT_RUNTIME_SYNC_MODE_TIMELINE)
            ? completed_timesteps - *previous_completed_timesteps
            : completed_timesteps;
    const rvrt_runtime_status_t status = sync_wait_payload_impl(
        session, thread_index, sync_payload, timeout_ms, rx_frames,
        rx_frame_count, rx_frame_handler, user_data);
    if (status == RVRT_RUNTIME_OK) {
        *sync_mode = RVRT_RUNTIME_SYNC_MODE_TIMELINE;
        *previous_completed_timesteps = completed_timesteps;
    }
    return status;
}

void paicore_noc_handler(void)
{
    rvrt_transport_t *const session = g_active_session;
#if RVRT_ENABLE_STATS
    const bool count_irq = (session != NULL) && session->rx_barrier.active;
    const rv_counter_t irq_start = count_irq ? __get_rv_cycle() : 0U;
#endif
    SAVE_IRQ_CSR_CONTEXT();
    noc_irq_ack();
    noc_irq_disable();

    if (__RARELY((session == NULL) || !session->rx_barrier.active)) {
        RESTORE_IRQ_CSR_CONTEXT();
        return;
    }

    rvrt_runtime_rx_barrier_t *const rx_barrier = &session->rx_barrier;
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

#if RVRT_ENABLE_STATS
    if (count_irq) {
        session->stats.rx_irq_service_cycles += __get_rv_cycle() - irq_start;
        session->stats.rx_irq_count++;
    }
#endif

    RESTORE_IRQ_CSR_CONTEXT();
}

rvrt_runtime_status_t rvrt_transport_get_stats(const rvrt_transport_t *session,
                                               rvrt_runtime_stats_t *stats)
{
    if (__RARELY((session == NULL) || (session->artifact == NULL) ||
                 (stats == NULL))) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

#if RVRT_ENABLE_STATS
    const rvrt_runtime_stats_t snapshot = session->stats;
    *stats = snapshot;
#else
    *stats = (rvrt_runtime_stats_t){0};
#endif
    return RVRT_RUNTIME_OK;
}

const char *rvrt_transport_status_string(rvrt_runtime_status_t status)
{
    switch (status) {
        case RVRT_RUNTIME_OK:
            return "ok";
        case RVRT_RUNTIME_TIMEOUT:
            return "timeout";
        case RVRT_RUNTIME_BUFFER_TOO_SMALL:
            return "buffer too small";
        case RVRT_RUNTIME_OVERFLOW:
            return "overflow";
        case RVRT_RUNTIME_HARDWARE_ERROR:
            return "hardware error";
        case RVRT_RUNTIME_RUNTIME_ERROR:
            return "runtime error";
        case RVRT_RUNTIME_SYNC_MODE_ERROR:
            return "sync mode error";
        case RVRT_RUNTIME_SCHEDULE_UNSUPPORTED:
            return "schedule unsupported";
        case RVRT_RUNTIME_FAULTED:
            return "session faulted";
        case RVRT_RUNTIME_BUSY:
            return "session busy";
        default:
            return "unknown";
    }
}
