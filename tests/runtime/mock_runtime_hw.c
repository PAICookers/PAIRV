#include "mock_runtime_hw.h"

#include <stddef.h>
#include <string.h>

#include "rvrt_tasks.h"

#define MOCK_FRAME_CAPACITY 1024U

static rvrt_frame_t g_sent[MOCK_FRAME_CAPACITY];
static uint32_t g_sent_count;
static rvrt_frame_t g_rx[MOCK_FRAME_CAPACITY];
static uint32_t g_rx_count;
static uint32_t g_rx_index;
static bool g_irq_enabled;
static bool g_auto_irq;
static rv_counter_t g_cycles;
static void (*g_irq_handler)(void);
static rvrt_session_t *g_probe_session;
static rvrt_session_status_t g_nested_send_status;
static rvrt_session_status_t g_nested_sync_status;
static uint32_t g_task_run_count;

void mock_runtime_reset(void)
{
    g_sent_count = 0U;
    g_rx_count = 0U;
    g_rx_index = 0U;
    g_irq_enabled = false;
    g_auto_irq = true;
    g_cycles = 0U;
    g_irq_handler = NULL;
    g_probe_session = NULL;
    g_nested_send_status = RVRT_SESSION_OK;
    g_nested_sync_status = RVRT_SESSION_OK;
    g_task_run_count = 0U;
}

void mock_runtime_queue_rx(const rvrt_frame_t *frames, uint32_t frame_count)
{
    g_rx_count = frame_count;
    g_rx_index = 0U;
    if ((frames != NULL) && (frame_count <= MOCK_FRAME_CAPACITY)) {
        memcpy(g_rx, frames, frame_count * sizeof(frames[0]));
    }
}

void mock_runtime_set_auto_irq(bool enabled) { g_auto_irq = enabled; }

void mock_runtime_probe_armed(rvrt_session_t *session)
{
    g_probe_session = session;
}

uint32_t mock_runtime_sent_count(void) { return g_sent_count; }

const rvrt_frame_t *mock_runtime_sent_frames(void) { return g_sent; }

rvrt_session_status_t mock_runtime_nested_send_status(void)
{
    return g_nested_send_status;
}

rvrt_session_status_t mock_runtime_nested_sync_status(void)
{
    return g_nested_sync_status;
}

uint32_t mock_runtime_task_run_count(void) { return g_task_run_count; }

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

void noc_fifo_write_frame_words(uint32_t high, uint32_t low)
{
    record_sent(high, low);
    if (g_probe_session != NULL) {
        const rvrt_frame_t frame = {0U, 0U};
        const rvrt_frame_t *nested_frames = NULL;
        uint32_t nested_count = 0U;
        g_nested_send_status =
            rvrt_session_send_frames(g_probe_session, &frame, 1U);
        g_nested_sync_status = rvrt_session_sync_wait(
            g_probe_session, 1U, 1U, &nested_frames, &nested_count);
        g_probe_session = NULL;
    }
    if (g_auto_irq && g_irq_enabled && (g_irq_handler != NULL)) {
        g_irq_handler();
    }
}

void noc_fifo_write_frame_words_unlocked(uint32_t high, uint32_t low)
{
    record_sent(high, low);
}

int32_t noc_fifo_read_frame_words(uint32_t *high, uint32_t *low)
{
    if ((high == NULL) || (low == NULL) || (g_rx_index >= g_rx_count)) {
        return -1;
    }
    *high = g_rx[g_rx_index].high;
    *low = g_rx[g_rx_index].low;
    g_rx_index++;
    return 0;
}

uint32_t noc_irq_pending(void) { return g_rx_index < g_rx_count ? 1U : 0U; }

bool noc_irq_is_enabled(void) { return g_irq_enabled; }

void noc_irq_ack(void) {}

void noc_irq_enable(void) { g_irq_enabled = true; }

void noc_irq_disable(void) { g_irq_enabled = false; }

rvrt_task_status_t rvrt_task_run(uint32_t cpu_task_index,
                                 const rvrt_task_io_t *io)
{
    if ((cpu_task_index != 0U) || (io == NULL) || (io->input == NULL) ||
        (io->output == NULL) || (io->output_size < io->input_size)) {
        return RVRT_TASK_STATUS_BAD_ARGUMENT;
    }
    memcpy(io->output, io->input, io->input_size);
    g_task_run_count++;
    return RVRT_TASK_STATUS_OK;
}
