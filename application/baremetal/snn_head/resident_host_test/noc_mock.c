#include "evalsoc_noc.h"
#include "frame_codec.h"
#include "nuclei_sdk_soc.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

extern uint32_t resident_test_control(uint32_t, uint32_t, rvrt_frame_t *,
                                      uint32_t);
static void (*handler)(void);
static bool enabled, servicing;
static rv_counter_t cycles;
static rvrt_frame_t frames[6145];
static uint32_t count, cursor;
static bool startup = true;
static uint64_t startup_writes;
static uint64_t expected_config_writes;
static uint32_t illegal_inference_writes;

static void pump(void)
{
    if (servicing)
        return;
    servicing = true;
    while (enabled && handler && cursor < count) {
        const uint32_t before = cursor;
        handler();
        assert(cursor > before || !enabled);
    }
    servicing = false;
}

int32_t ECLIC_Register_IRQ(int32_t irq, uint8_t shv, uint8_t trigger,
                           uint8_t level, uint8_t priority, void *fn)
{
    (void)irq;
    (void)shv;
    (void)trigger;
    (void)level;
    (void)priority;
    handler = (void (*)(void))fn;
    return 0;
}
rv_counter_t __get_rv_cycle(void)
{
    pump();
    return ++cycles;
}
void noc_irq_enable(void)
{
    enabled = true;
    pump();
}
void noc_irq_disable(void) { enabled = false; }
void noc_irq_ack(void) {}
bool noc_irq_is_enabled(void) { return enabled; }
uint32_t noc_irq_pending(void) { return cursor < count; }
int32_t noc_fifo_read_frame_words(uint32_t *high, uint32_t *low)
{
    if (!high || !low || cursor == count)
        return -1;
    *high = frames[cursor].high;
    *low = frames[cursor++].low;
    return 0;
}

static void write_frame(uint32_t high, uint32_t low)
{
    if (startup && startup_writes < expected_config_writes) {
        ++startup_writes;
        return;
    }
    const uint32_t kind = high >> 28;
    if (kind == 0xC || kind == 0xD) {
        assert(cursor == count);
        cursor = 0;
        count = resident_test_control(high, low, frames, 6145);
        pump();
    } else if (kind != 0x8 && kind != 0x9) {
        ++illegal_inference_writes;
    }
}
void noc_fifo_write_frame_words_unlocked(uint32_t high, uint32_t low)
{
    write_frame(high, low);
}
void noc_fifo_write_frame_words(uint32_t high, uint32_t low)
{
    write_frame(high, low);
}
void resident_test_start_inference(void) { startup = false; }
void resident_test_expect_config_frames(uint64_t frames_count)
{
    expected_config_writes = frames_count;
}
uint64_t resident_test_startup_writes(void) { return startup_writes; }
uint32_t resident_test_illegal_writes(void) { return illegal_inference_writes; }
