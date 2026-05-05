#include "evalsoc_noc.h"

#if defined(RTOS_FREERTOS)
#include "FreeRTOS.h"
#include "task.h"
#endif

#ifndef NOC_USE_FREERTOS_CRITICAL
#define NOC_USE_FREERTOS_CRITICAL 1
#endif

#if defined(RTOS_FREERTOS) && NOC_USE_FREERTOS_CRITICAL
#define NOC_FREERTOS_CRITICAL 1
#else
#define NOC_FREERTOS_CRITICAL 0
#endif

static volatile uint32_t *const noc_down_fifo =
    (volatile uint32_t *)NOC_FIFO_DOWN_ADDR;
static volatile uint32_t *const noc_up_fifo =
    (volatile uint32_t *)NOC_FIFO_UP_ADDR;

/*
 * Enter the execution-context critical section used for multiword NoC FIFO
 * accesses. The policy differs between bare metal and FreeRTOS, but callers do
 * not need to care about that split.
 */
void noc_enter_critical(noc_lock_state_t *state)
{
#if NOC_FREERTOS_CRITICAL
    (void)state;
    taskENTER_CRITICAL();
#else
    state->mstatus = __RV_CSR_READ(CSR_MSTATUS);
    __disable_irq();
#endif
}

/* Restore the execution-context state captured by noc_enter_critical(). */
void noc_exit_critical(const noc_lock_state_t *state)
{
#if NOC_FREERTOS_CRITICAL
    (void)state;
    taskEXIT_CRITICAL();
#else
    if ((state->mstatus & MSTATUS_MIE) != 0U) {
        __enable_irq();
    } else {
        __disable_irq();
    }
#endif
}

/* ISR companion to noc_enter_critical() for callers that are already running
 * under an interrupt prologue. */
__STATIC_INLINE void noc_enter_critical_from_isr(noc_lock_state_t *state)
{
#if NOC_FREERTOS_CRITICAL
    state->os_state = (uintptr_t)taskENTER_CRITICAL_FROM_ISR();
#else
    state->mstatus = __RV_CSR_READ(CSR_MSTATUS);
    __disable_irq();
#endif
}

/* ISR companion to noc_exit_critical(). */
__STATIC_INLINE void noc_exit_critical_from_isr(const noc_lock_state_t *state)
{
#if NOC_FREERTOS_CRITICAL
    taskEXIT_CRITICAL_FROM_ISR((UBaseType_t)state->os_state);
#else
    if ((state->mstatus & MSTATUS_MIE) != 0U) {
        __enable_irq();
    } else {
        __disable_irq();
    }
#endif
}

/* Low-level two-word FIFO write primitive. Callers must provide exclusion.
 * The two MMIO stores happen in exactly the parameter order: fifo_word0, then
 * fifo_word1. */
void noc_fifo_write_words_unlocked(uint32_t fifo_word0, uint32_t fifo_word1)
{
    __WMB();
    *noc_down_fifo = fifo_word0;
    *noc_down_fifo = fifo_word1;
    __WMB();
}

/* Protected two-word FIFO write for task/baremetal thread context. */
void noc_fifo_write_words(uint32_t fifo_word0, uint32_t fifo_word1)
{
    noc_lock_state_t state;

    noc_enter_critical(&state);
    noc_fifo_write_words_unlocked(fifo_word0, fifo_word1);
    noc_exit_critical(&state);
}

/* Protected two-word FIFO write for interrupt context. */
void noc_fifo_write_words_from_isr(uint32_t fifo_word0, uint32_t fifo_word1)
{
    noc_lock_state_t state;

    noc_enter_critical_from_isr(&state);
    noc_fifo_write_words_unlocked(fifo_word0, fifo_word1);
    noc_exit_critical_from_isr(&state);
}

void noc_fifo_write_frame_words(uint32_t frame_high32, uint32_t frame_low32)
{
    noc_fifo_write_words(frame_low32, frame_high32);
}

void noc_fifo_write_frame_words_unlocked(uint32_t frame_high32,
                                         uint32_t frame_low32)
{
    noc_fifo_write_words_unlocked(frame_low32, frame_high32);
}

void noc_fifo_write_frame_words_from_isr(uint32_t frame_high32,
                                         uint32_t frame_low32)
{
    noc_fifo_write_words_from_isr(frame_low32, frame_high32);
}

void noc_fifo_write_frame64(uint64_t frame)
{
    uint32_t hi32 = (uint32_t)(frame >> 32);
    uint32_t lo32 = (uint32_t)(frame & 0xFFFFFFFFULL);
    noc_fifo_write_frame_words(hi32, lo32);
}

void noc_fifo_write_frame64_unlocked(uint64_t frame)
{
    uint32_t hi32 = (uint32_t)(frame >> 32);
    uint32_t lo32 = (uint32_t)(frame & 0xFFFFFFFFULL);
    noc_fifo_write_frame_words_unlocked(hi32, lo32);
}

void noc_fifo_write_frame64_from_isr(uint64_t frame)
{
    uint32_t hi32 = (uint32_t)(frame >> 32);
    uint32_t lo32 = (uint32_t)(frame & 0xFFFFFFFFULL);
    noc_fifo_write_frame_words_from_isr(hi32, lo32);
}

/* Uplink reads preserve FIFO word order and return the words separately. */
int32_t noc_fifo_read_words(uint32_t *fifo_word0, uint32_t *fifo_word1)
{
    if (__RARELY(fifo_word0 == NULL) || __RARELY(fifo_word1 == NULL)) {
        return -1;
    }

    __RMB();
    *fifo_word0 = *noc_up_fifo;
    *fifo_word1 = *noc_up_fifo;
    __RMB();

    return 0;
}

int32_t noc_fifo_read_frame_words(uint32_t *frame_high32, uint32_t *frame_low32)
{
    return noc_fifo_read_words(frame_high32, frame_low32);
}
