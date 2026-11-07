#include "mock_runtime_hw.h"
#include "transport_thread_internal.h"

#include <stddef.h>
#include <string.h>

#define MOCK_FRAME_CAPACITY 1024U

static rvrt_frame_t g_sent[MOCK_FRAME_CAPACITY];
static uint32_t g_sent_count;
static rvrt_frame_t g_rx[MOCK_FRAME_CAPACITY];
static uint32_t g_rx_count;
static uint32_t g_rx_available;
static uint32_t g_rx_index;
static uint32_t g_rx_segment_ends[32];
static uint32_t g_rx_segment_count;
static uint32_t g_rx_segment_index;
static bool g_irq_enabled;
static bool g_auto_irq;
static bool g_irq_on_enable;
static bool g_irq_on_disable;
static bool g_fifo_read_error;
static bool g_in_irq;
static rv_counter_t g_cycles;
static void (*g_irq_handler)(void);
static rvrt_transport_t *g_probe_session;
static rvrt_runtime_status_t g_nested_send_status;
static rvrt_runtime_status_t g_nested_sync_status;

void mock_runtime_reset(void)
{
    g_sent_count = 0U;
    g_rx_count = 0U;
    g_rx_available = 0U;
    g_rx_index = 0U;
    g_rx_segment_count = 0U;
    g_rx_segment_index = 0U;
    g_irq_enabled = false;
    g_auto_irq = true;
    g_irq_on_enable = false;
    g_irq_on_disable = false;
    g_fifo_read_error = false;
    g_in_irq = false;
    g_cycles = 0U;
    g_irq_handler = NULL;
    g_probe_session = NULL;
    g_nested_send_status = RVRT_RUNTIME_OK;
    g_nested_sync_status = RVRT_RUNTIME_OK;
}

void mock_runtime_clear_sent(void) { g_sent_count = 0U; }

void mock_runtime_queue_rx(const rvrt_frame_t *frames, uint32_t frame_count)
{
    g_rx_count = frame_count;
    g_rx_available = 0U;
    g_rx_index = 0U;
    g_rx_segment_count = 0U;
    g_rx_segment_index = 0U;
    if ((frames != NULL) && (frame_count <= MOCK_FRAME_CAPACITY)) {
        memcpy(g_rx, frames, frame_count * sizeof(frames[0]));
        for (uint32_t index = 0U; index < frame_count; ++index) {
            if (rvrt_frame_is_complete(&frames[index]) &&
                (g_rx_segment_count <
                 (uint32_t)(sizeof(g_rx_segment_ends) /
                            sizeof(g_rx_segment_ends[0])))) {
                g_rx_segment_ends[g_rx_segment_count++] = index + 1U;
            }
        }
        if ((frame_count != 0U) &&
            ((g_rx_segment_count == 0U) ||
             (g_rx_segment_ends[g_rx_segment_count - 1U] != frame_count))) {
            g_rx_segment_ends[g_rx_segment_count++] = frame_count;
        }
    }
}

void mock_runtime_queue_rx_segments(const rvrt_frame_t *frames,
                                    uint32_t frame_count,
                                    const uint32_t *segment_frame_counts,
                                    uint32_t segment_count)
{
    g_rx_count = frame_count;
    g_rx_available = 0U;
    g_rx_index = 0U;
    g_rx_segment_count = 0U;
    g_rx_segment_index = 0U;
    if ((frames == NULL) || (frame_count > MOCK_FRAME_CAPACITY) ||
        (segment_frame_counts == NULL) ||
        (segment_count > (uint32_t)(sizeof(g_rx_segment_ends) /
                                    sizeof(g_rx_segment_ends[0])))) {
        return;
    }

    memcpy(g_rx, frames, frame_count * sizeof(frames[0]));
    uint32_t end = 0U;
    for (uint32_t index = 0U; index < segment_count; ++index) {
        if (segment_frame_counts[index] > frame_count - end) {
            g_rx_count = 0U;
            return;
        }
        end += segment_frame_counts[index];
        g_rx_segment_ends[index] = end;
    }
    if (end != frame_count) {
        g_rx_count = 0U;
        return;
    }
    g_rx_segment_count = segment_count;
}

void mock_runtime_set_auto_irq(bool enabled) { g_auto_irq = enabled; }

void mock_runtime_set_irq_on_enable(bool enabled) { g_irq_on_enable = enabled; }

void mock_runtime_set_irq_on_disable(bool enabled)
{
    g_irq_on_disable = enabled;
}

void mock_runtime_set_fifo_read_error(bool enabled)
{
    g_fifo_read_error = enabled;
}

void mock_runtime_probe_rx_barrier_active(rvrt_transport_t *session)
{
    g_probe_session = session;
}

uint32_t mock_runtime_sent_count(void) { return g_sent_count; }

const rvrt_frame_t *mock_runtime_sent_frames(void) { return g_sent; }

rvrt_runtime_status_t mock_runtime_nested_send_status(void)
{
    return g_nested_send_status;
}

rvrt_runtime_status_t mock_runtime_nested_sync_status(void)
{
    return g_nested_sync_status;
}

rv_counter_t __get_rv_cycle(void)
{
    g_cycles++;
    return g_cycles;
}

int32_t ECLIC_Register_IRQ(int32_t irqn, uint8_t shv, uint8_t trigger,
                           uint8_t level, uint8_t priority, void *handler)
{
    (void)irqn;
    (void)shv;
    (void)trigger;
    (void)level;
    (void)priority;
    g_irq_handler = (void (*)(void))handler;
    return 0;
}

static void record_sent(uint32_t high, uint32_t low)
{
    if (g_sent_count < MOCK_FRAME_CAPACITY) {
        g_sent[g_sent_count++] = (rvrt_frame_t){high, low};
    }
}

static bool is_control_frame(uint32_t high)
{
    const uint32_t frame_type = high >> 28U;
    return (frame_type == 0xCU) || (frame_type == 0xDU);
}

static void release_control_rx(uint32_t high)
{
    if (is_control_frame(high) && (g_rx_segment_index < g_rx_segment_count)) {
        g_rx_available = g_rx_segment_ends[g_rx_segment_index++];
    }
}

static void run_pending_irq(void)
{
    if (g_auto_irq && g_irq_enabled && (g_irq_handler != NULL) &&
        (g_rx_index < g_rx_available)) {
        g_in_irq = true;
        g_irq_handler();
        g_in_irq = false;
    }
}

void noc_fifo_write_frame_words(uint32_t high, uint32_t low)
{
    record_sent(high, low);
    release_control_rx(high);
    if (g_probe_session != NULL) {
        const rvrt_frame_t frame = {0U, 0U};
        const rvrt_frame_t *nested_frames = NULL;
        uint32_t nested_count = 0U;
        g_nested_send_status =
            rvrt_transport_send_frames(g_probe_session, &frame, 1U);
        rvrt_runtime_sync_mode_t sync_mode = RVRT_RUNTIME_SYNC_MODE_UNSET;
        g_nested_sync_status = rvrt_transport_sync_wait_payload_for_thread(
            g_probe_session, 0U, &sync_mode, 1U, 1U, &nested_frames,
            &nested_count);
        g_probe_session = NULL;
    }
    run_pending_irq();
}

void noc_fifo_write_frame_words_unlocked(uint32_t high, uint32_t low)
{
    record_sent(high, low);
    release_control_rx(high);
    run_pending_irq();
}

int32_t noc_fifo_read_frame_words(uint32_t *high, uint32_t *low)
{
    if (g_fifo_read_error) {
        return -1;
    }
    if ((high == NULL) || (low == NULL) || (g_rx_index >= g_rx_available)) {
        return -1;
    }
    *high = g_rx[g_rx_index].high;
    *low = g_rx[g_rx_index].low;
    g_rx_index++;
    return 0;
}

uint32_t noc_irq_pending(void) { return g_rx_index < g_rx_available ? 1U : 0U; }

bool noc_irq_is_enabled(void) { return g_irq_enabled; }

void noc_irq_ack(void) {}

void noc_irq_enable(void)
{
    g_irq_enabled = true;
    if (g_auto_irq && !g_in_irq && (g_irq_handler != NULL) &&
        noc_irq_pending()) {
        g_in_irq = true;
        g_irq_on_enable = false;
        g_irq_handler();
        g_in_irq = false;
    }
}

void noc_irq_disable(void)
{
    if (g_irq_on_disable && g_irq_enabled && (g_irq_handler != NULL) &&
        noc_irq_pending()) {
        g_irq_on_disable = false;
        g_irq_handler();
    }
    g_irq_enabled = false;
}
