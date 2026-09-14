/* Host transport fixture: missing RX returns an MMIO error; hardware can block.
 */
#include "evalsoc_noc.h"
#include "frame_codec.h"
#include "nuclei_sdk_soc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOST_MOCK_COMPLETE_HIGH 0xE0000000U

static void (*g_handler)(void);
static bool g_irq_enabled;
static rv_counter_t g_cycles;

static const rvrt_frame_t *g_rx_queue;
static uint32_t g_rx_len;
static uint32_t g_rx_pos;
static uint32_t g_rx_available;
static uint32_t g_next_generated_split;
static bool g_deferred_rx;
static bool g_in_handler;
static uint32_t g_sent_count;

void host_mock_set_rx_frames(const rvrt_frame_t *frames, uint32_t count)
{
    g_rx_queue = frames;
    g_rx_len = count;
    g_rx_pos = 0U;
    g_rx_available = count;
}

void host_mock_set_next_generated_split(uint32_t first_batch_count)
{
    g_next_generated_split = first_batch_count;
}

uint32_t host_mock_sent_count(void) { return g_sent_count; }

typedef uint32_t (*host_mock_gen_fn)(void *ctx, uint32_t control_high,
                                     uint32_t control_low, rvrt_frame_t *out,
                                     uint32_t cap);
static host_mock_gen_fn g_gen;
static void *g_gen_ctx;
#define HOST_MOCK_GEN_CAP 4096U
static rvrt_frame_t g_gen_buf[HOST_MOCK_GEN_CAP];

void host_mock_set_generator(host_mock_gen_fn fn, void *ctx)
{
    g_gen = fn;
    g_gen_ctx = ctx;
    g_rx_queue = NULL;
    g_rx_len = 0U;
    g_rx_pos = 0U;
    g_rx_available = 0U;
    g_next_generated_split = 0U;
    g_deferred_rx = false;
    g_sent_count = 0U;
}

int32_t ECLIC_Register_IRQ(int32_t irqn, uint8_t shv, uint8_t trigger,
                           uint8_t level, uint8_t priority, void *handler)
{
    (void)irqn;
    (void)shv;
    (void)trigger;
    (void)level;
    (void)priority;
    g_handler = (void (*)(void))handler;
    return 0;
}

static void deliver_pending_irq(void)
{
    if (!g_in_handler && g_irq_enabled && (g_handler != NULL) &&
        (noc_irq_pending() != 0U)) {
        g_in_handler = true;
        g_handler();
        g_in_handler = false;
    }
}

rv_counter_t __get_rv_cycle(void)
{
    ++g_cycles;
    if (g_deferred_rx && !g_in_handler) {
        g_rx_available = g_rx_len;
        g_deferred_rx = false;
    }
    deliver_pending_irq();
    return g_cycles;
}

void noc_irq_enable(void) { g_irq_enabled = true; }
void noc_irq_disable(void) { g_irq_enabled = false; }
void noc_irq_ack(void) {}
bool noc_irq_is_enabled(void) { return g_irq_enabled; }
uint32_t noc_irq_pending(void)
{
    return ((g_rx_queue != NULL) && (g_rx_pos < g_rx_available)) ? 1U : 0U;
}

int32_t noc_fifo_read_frame_words(uint32_t *high, uint32_t *low)
{
    if ((high == NULL) || (low == NULL)) {
        return -1;
    }
    if ((g_rx_queue != NULL) && (g_rx_pos < g_rx_available)) {
        *high = g_rx_queue[g_rx_pos].high;
        *low = g_rx_queue[g_rx_pos].low;
        ++g_rx_pos;
        return 0;
    }
    if (g_gen != NULL) {
        return -1;
    }

    *high = HOST_MOCK_COMPLETE_HIGH;
    *low = 0U;
    return 0;
}

static void deliver_control(uint32_t high, uint32_t low)
{
    if (g_gen != NULL) {
        const uint32_t n =
            g_gen(g_gen_ctx, high, low, g_gen_buf, HOST_MOCK_GEN_CAP);
        g_rx_queue = g_gen_buf;
        g_rx_len = n;
        g_rx_pos = 0U;
        g_rx_available = n;
        if ((g_next_generated_split != 0U) && (g_next_generated_split < n)) {
            g_rx_available = g_next_generated_split;
            g_next_generated_split = 0U;
            g_deferred_rx = true;
        }
    } else if ((g_rx_queue == NULL) || (g_rx_pos >= g_rx_len)) {
        static const rvrt_frame_t default_complete = {
            HOST_MOCK_COMPLETE_HIGH,
            0U,
        };
        g_rx_queue = &default_complete;
        g_rx_len = 1U;
        g_rx_pos = 0U;
        g_rx_available = 1U;
    }
    deliver_pending_irq();
}

void noc_fifo_write_frame_words_unlocked(uint32_t high, uint32_t low)
{
    ++g_sent_count;
    const uint32_t type = high >> 28U;
    if (g_irq_enabled && ((type == 0xCU) || (type == 0xDU))) {
        deliver_control(high, low);
    }
}

void noc_fifo_write_frame_words(uint32_t high, uint32_t low)
{
    ++g_sent_count;
    deliver_control(high, low);
}
