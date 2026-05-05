/*
 * NoC transport helpers for EvalSoC.
 *
 * The hardware FIFO itself only sees an ordered stream of 32-bit words.
 * Callers, however, often think in terms of a legal 64-bit frame with a
 * logical `high32` half and `low32` half.
 *
 * To keep those two viewpoints separate:
 * - "word" APIs describe the exact FIFO word sequence only
 * - "frame" APIs describe a logical 64-bit frame split into `high32`/`low32`
 *
 * Historical note from `Nuclei/UART/application/main.c`:
 * - uplink reads pop the first returned word into the logical `high32` slot,
 *   then the second returned word into the logical `low32` slot
 * - downlink writes for many generated `uint32_t` arrays do not pass adjacent
 *   array elements straight through, because the array storage order and the
 *   proven FIFO emission order are not guaranteed to match
 *
 * In particular, the legacy UART config-frame loop sends:
 * `write_64bit_to_fifo(config_frame[i + 1], config_frame[i])`
 * while the generated config array stores one legal frame as adjacent
 * `{high32, low32}` words. So for that array family, the effective FIFO
 * emission order is "low32 first, then high32", even though the helper itself
 * still stores its first parameter before its second.
 *
 * For writes, keep the same distinction in mind:
 * - frame APIs are named in logical frame terms: `frame_high32`,
 *   `frame_low32`
 * - raw word APIs are named in FIFO-order terms: `fifo_word0`, `fifo_word1`
 * - the helper's parameter order and the effective frame order seen on the FIFO
 *   are not the same question when call sites intentionally swap adjacent array
 *   elements before invoking a frame API
 *
 * Use this driver instead of writing NoC FIFO addresses directly from
 * applications, so ordering and critical-section policy remain centralized.
 */

#ifndef _EVALSOC_NOC_H
#define _EVALSOC_NOC_H

#include "evalsoc.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct noc_lock_state {
    unsigned long mstatus;
    uintptr_t os_state;
} noc_lock_state_t;

#define NOC_IRQ_REG_ADDR        (EVALSOC_PERIPH_BASE + 0x00000UL)
#define NOC_FIFO_DOWN_ADDR      (EVALSOC_PERIPH_BASE + 0x08000UL)
#define NOC_FIFO_UP_ADDR        (EVALSOC_PERIPH_BASE + 0x10000UL)
#define NOC_CLILINTIE_OFFSET    (0x10BDUL)
#define NOC_CLILINTIE_ADDR      (__ECLIC_BASEADDR + NOC_CLILINTIE_OFFSET)

#define NOC_MMIO32(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define NOC_MMIO8(addr)         (*(volatile uint8_t *)(uintptr_t)(addr))

/**
 * Write two 32-bit words to the NoC downlink FIFO in the exact order supplied.
 *
 * @param fifo_word0
 * The first 32-bit word written to the FIFO in this call.
 * @param fifo_word1
 * The second 32-bit word written to the FIFO in this call.
 *
 * @note
 * This is a raw ordered-word primitive. It says nothing about whether the
 * caller intends those two words to represent `low32 -> high32`,
 * `high32 -> low32`, or some other pairwise convention.
 *
 * @note
 * This variant protects the two-word sequence against interruption in the
 * current runtime model before touching the downlink FIFO.
 */
void noc_fifo_write_words(uint32_t fifo_word0, uint32_t fifo_word1);

/**
 * ISR-safe form of noc_fifo_write_words().
 *
 * @param fifo_word0
 * The first 32-bit word written to the FIFO in this call.
 * @param fifo_word1
 * The second 32-bit word written to the FIFO in this call.
 *
 * @note
 * Use this only when the caller is already executing in interrupt context.
 */
void noc_fifo_write_words_from_isr(uint32_t fifo_word0, uint32_t fifo_word1);

/**
 * Write two ordered 32-bit words without taking any lock or masking any IRQ.
 *
 * @param fifo_word0
 * The first 32-bit word written to the FIFO in this call.
 * @param fifo_word1
 * The second 32-bit word written to the FIFO in this call.
 *
 * Callers must already have established whatever exclusion the active runtime
 * requires before using this helper.
 */
void noc_fifo_write_words_unlocked(uint32_t fifo_word0, uint32_t fifo_word1);

/**
 * Write one legal 64-bit NoC frame expressed as logical `(high32, low32)`.
 *
 * @param frame_high32
 * Logical frame bits `[63:32]`.
 * @param frame_low32
 * Logical frame bits `[31:0]`.
 *
 * @note
 * This is the preferred public API when the caller is thinking in frame terms
 * rather than raw FIFO word order.
 *
 * @note
 * The logical `(high32, low32)` naming here is independent of how a local
 * `uint32_t` array stores adjacent words. If a generated array stores one frame
 * as `{high32, low32}`, but the proven send path needs `words[i + 1]` before
 * `words[i]`, that swap belongs at the call site.
 *
 * @note
 * The effective downlink FIFO emission order used by this frame API is
 * `frame_low32` first, then `frame_high32`. That keeps the API focused on
 * logical frame halves while centralizing the proven downlink word-order policy
 * inside the driver.
 *
 * @note
 * Callers therefore should pass the legal frame halves as
 * `(frame_high32, frame_low32)` and should not manually swap them again.
 */
void noc_fifo_write_frame_words(uint32_t frame_high32, uint32_t frame_low32);

/**
 * ISR-safe form of noc_fifo_write_frame_words().
 *
 * @param frame_high32
 * Logical frame bits `[63:32]`.
 * @param frame_low32
 * Logical frame bits `[31:0]`.
 */
void noc_fifo_write_frame_words_from_isr(uint32_t frame_high32,
                                         uint32_t frame_low32);

/**
 * Unlocked form of noc_fifo_write_frame_words().
 *
 * @param frame_high32
 * Logical frame bits `[63:32]`.
 * @param frame_low32
 * Logical frame bits `[31:0]`.
 *
 * @note
 * Uses the same effective downlink FIFO emission order as
 * `noc_fifo_write_frame_words()`: `frame_low32` first, then `frame_high32`.
 *
 * Callers typically use this inside an explicit batch send region.
 */
void noc_fifo_write_frame_words_unlocked(uint32_t frame_high32,
                                         uint32_t frame_low32);

/**
 * Write one legal 64-bit NoC frame from a packed integer value.
 *
 * @param frame
 * Packed logical frame value. The helper splits it into logical `high32` and
 * `low32` halves before forwarding to `noc_fifo_write_frame_words()`.
 */
void noc_fifo_write_frame64(uint64_t frame);

/**
 * ISR-safe form of noc_fifo_write_frame64().
 *
 * @param frame
 * Packed logical frame value.
 */
void noc_fifo_write_frame64_from_isr(uint64_t frame);

/**
 * Unlocked form of noc_fifo_write_frame64().
 *
 * @param frame
 * Packed logical frame value.
 */
void noc_fifo_write_frame64_unlocked(uint64_t frame);

/**
 * Enter the runtime-specific critical section used to keep multiword NoC FIFO
 * accesses contiguous.
 *
 * This only protects CPU/RTOS execution context. It does not implicitly touch
 * the NoC device IRQ enable bit.
 */
void noc_enter_critical(noc_lock_state_t *state);

/**
 * Leave the critical section previously entered by noc_enter_critical().
 */
void noc_exit_critical(const noc_lock_state_t *state);

/**
 * Read two ordered 32-bit words from the uplink FIFO into caller storage.
 *
 * @param fifo_word0
 * Receives the first 32-bit word popped from the uplink FIFO.
 * @param fifo_word1
 * Receives the second 32-bit word popped from the uplink FIFO.
 *
 * @retval 0
 * Success.
 * @retval -1
 * At least one output pointer was `NULL`.
 */
int32_t noc_fifo_read_words(uint32_t *fifo_word0, uint32_t *fifo_word1);

/**
 * Read one legal 64-bit frame from the uplink FIFO as logical `(high32,
 * low32)`.
 *
 * @param frame_high32
 * Receives the logical frame bits `[63:32]`.
 * @param frame_low32
 * Receives the logical frame bits `[31:0]`.
 *
 * @note
 * This matches the legacy UART app readback path: the first popped uplink word
 * is treated as the logical `high32`, then the second popped word is treated
 * as the logical `low32`.
 */
int32_t noc_fifo_read_frame_words(uint32_t *frame_high32,
                                  uint32_t *frame_low32);

/** Return the raw NoC IRQ pending register value. */
__STATIC_FORCEINLINE uint32_t noc_irq_pending(void)
{
    return (uint32_t)NOC_MMIO32(NOC_IRQ_REG_ADDR);
}

/** Return whether the NoC interrupt source is currently enabled at ECLIC. */
__STATIC_FORCEINLINE bool noc_irq_is_enabled(void)
{
    return NOC_MMIO8(NOC_CLILINTIE_ADDR) != 0U;
}

/** Acknowledge the active NoC interrupt at the device-side pending register. */
__STATIC_FORCEINLINE void noc_irq_ack(void)
{
    NOC_MMIO32(NOC_IRQ_REG_ADDR) = 0U;
}

/** Enable the NoC interrupt source at ECLIC. */
__STATIC_FORCEINLINE void noc_irq_enable(void)
{
    NOC_MMIO8(NOC_CLILINTIE_ADDR) = 1U;
}

/** Disable the NoC interrupt source at ECLIC. */
__STATIC_FORCEINLINE void noc_irq_disable(void)
{
    NOC_MMIO8(NOC_CLILINTIE_ADDR) = 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* _EVALSOC_NOC_H */
