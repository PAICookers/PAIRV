/* Only the platform IRQ/FIFO boundary is mocked; the runtime stays real. */
#include "evalsoc_noc.h"
#include "frame_codec.h"
#include "nuclei_sdk_soc.h"

#include <stddef.h>

static void (*irq_handler)(void);
static bool irq_enabled;
static rv_counter_t cycles;
static const rvrt_frame_t complete = {0xE0000000U, 0U};
static const rvrt_frame_t *sync_frames;
static uint32_t sync_counts[2];
static uint32_t sync_index;
static const rvrt_frame_t *rx_frames;
static uint32_t rx_count;

void mock_set_sync_frames(const rvrt_frame_t *frames, uint32_t first_count,
                          uint32_t second_count)
{
    sync_frames = frames;
    sync_counts[0] = first_count;
    sync_counts[1] = second_count;
}

int32_t ECLIC_Register_IRQ(int32_t irqn, uint8_t shv, uint8_t trigger,
                           uint8_t level, uint8_t priority, void *handler)
{
    (void)irqn;
    (void)shv;
    (void)trigger;
    (void)level;
    (void)priority;
    irq_handler = (void (*)(void))handler;
    return 0;
}

rv_counter_t __get_rv_cycle(void) { return ++cycles; }
void noc_irq_enable(void) { irq_enabled = true; }
void noc_irq_disable(void) { irq_enabled = false; }
void noc_irq_ack(void) {}
bool noc_irq_is_enabled(void) { return irq_enabled; }
uint32_t noc_irq_pending(void) { return rx_count != 0U; }

int32_t noc_fifo_read_frame_words(uint32_t *high, uint32_t *low)
{
    if (rx_count == 0U) {
        return -1;
    }
    *high = rx_frames->high;
    *low = rx_frames->low;
    ++rx_frames;
    --rx_count;
    return 0;
}

void noc_fifo_write_frame_words_unlocked(uint32_t high, uint32_t low)
{
    (void)high;
    (void)low;
}

void noc_fifo_write_frame_words(uint32_t high, uint32_t low)
{
    (void)low;
    if ((high >> 28U) == 0xDU) {
        sync_index = 0U;
        rx_frames = &complete;
        rx_count = 1U;
    } else if (((high >> 28U) == 0xCU) && (sync_index < 2U)) {
        rx_frames = sync_frames + (sync_index == 0U ? 0U : sync_counts[0]);
        rx_count = sync_counts[sync_index++];
    }
    if (irq_enabled && (irq_handler != NULL) && (rx_count != 0U)) {
        irq_handler();
    }
}
