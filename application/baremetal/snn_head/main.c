/*
 * Production UART service entry for the complete five-layer SNN Head chain.
 * Protocol parsing and validation live in snn_head_uart.c; this file only
 * binds them to EvalSoc UART, the cycle counter, and snn_head_run_chunk().
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "evalsoc_uart.h"
#include "nuclei_sdk_hal.h"
#include "nuclei_sdk_soc.h"
#include "snn_head.h"
#include "snn_head_profile.h"
#include "snn_head_uart.h"

#ifndef SNN_HEAD_UART_IO_TIMEOUT_MS
#define SNN_HEAD_UART_IO_TIMEOUT_MS 1000U
#endif

#if SNN_HEAD_TIMING
/* Timing-enabled builds retain the most recent inference metadata. */
volatile uint64_t snn_head_demo_last_cycles;
volatile uint32_t snn_head_demo_timer_hz;
#endif
#if RV_DEBUG_ENABLE_LOGGING
volatile bool snn_head_demo_success;
volatile int snn_head_demo_last_uart_status;

#define SNN_HEAD_DEBUG_EVENT_BYTES 160U
static char snn_head_debug_event[SNN_HEAD_DEBUG_EVENT_BYTES];
static bool snn_head_debug_event_valid;

static void capture_debug_error(rv_debug_level_t level, const char *title,
                                const char *function_name, const char *message,
                                void *user_data)
{
    (void)level;
    (void)user_data;
    if (snn_head_debug_event_valid) {
        return;
    }
    (void)snprintf(snn_head_debug_event, sizeof(snn_head_debug_event),
                   "%s:%s:%s", title != NULL ? title : "debug",
                   function_name != NULL ? function_name : "-",
                   message != NULL ? message : "");
    snn_head_debug_event_valid = true;
}
#endif

/* Keep full tensors off the small bare-metal stack and reuse them per request.
 */
static float input_buffer[SNN_HEAD_INPUT_FLOAT_COUNT];
static float action_buffer[SNN_HEAD_ACTION_FLOAT_COUNT];
static uint8_t result_wire[SNN_HEAD_UART_RESULT_PAYLOAD_BYTES];

static int uart_write_bytes(void *ctx, const uint8_t *data, size_t length,
                            uint64_t deadline);

static uint32_t read_hz(void *ctx)
{
    (void)ctx;
    return (uint32_t)SystemCoreClock;
}

static uint64_t uart_now(void *ctx)
{
    (void)ctx;
    return __get_rv_cycle();
}

static int uart_read_bytes(void *ctx, uint8_t *data, size_t length,
                           uint64_t deadline)
{
    UART_TypeDef *const uart = ctx;

    for (size_t index = 0U; index < length; ++index) {
        uint32_t value;
        do {
            value = uart->RXFIFO;
            if (__get_rv_cycle() >= deadline) {
                return SNN_HEAD_UART_ERR_TIMEOUT;
            }
        } while ((value & UART_RXFIFO_EMPTY) != 0U);
        data[index] = (uint8_t)value;
    }
    return SNN_HEAD_UART_OK;
}

#if RV_DEBUG_ENABLE_LOGGING || SNN_HEAD_TIMING
static uint64_t uart_deadline(void)
{
    const uint64_t ticks =
        (((uint64_t)SNN_HEAD_UART_IO_TIMEOUT_MS * SystemCoreClock) + 999U) /
        1000U;
    return __get_rv_cycle() + ticks;
}

#endif

#if RV_DEBUG_ENABLE_LOGGING
static bool has_terminal_frame(int status)
{
    return status == SNN_HEAD_UART_OK || status == SNN_HEAD_UART_ERR_HEADER ||
           status == SNN_HEAD_UART_ERR_PAYLOAD ||
           status == SNN_HEAD_UART_ERR_SHAPE ||
           status == SNN_HEAD_UART_ERR_INFERENCE;
}

static void write_debug_summary(int status)
{
    char line[256U];
    const char *const event =
        snn_head_debug_event_valid ? snn_head_debug_event : "none";
    const int formatted =
        snprintf(line, sizeof(line) - 1U,
                 "SNN_HEAD_DEBUG status=%d inference_ok=%u event=%s", status,
                 snn_head_demo_success ? 1U : 0U, event);
    if (formatted < 0) {
        return;
    }
    size_t length = (size_t)formatted;
    if (length >= sizeof(line) - 1U) {
        length = sizeof(line) - 2U;
    }
    line[length++] = '\n';
    (void)uart_write_bytes(SOC_DEBUG_UART, (const uint8_t *)line, length,
                           uart_deadline());
}
#endif

#if SNN_HEAD_TIMING
typedef struct {
    UART_TypeDef *uart;
    uint64_t deadline;
} timing_writer_t;

static bool timing_write(void *ctx, const char *data, size_t length)
{
    timing_writer_t *const writer = ctx;
    return uart_write_bytes(writer->uart, (const uint8_t *)data, length,
                            writer->deadline) == SNN_HEAD_UART_OK;
}

static bool log_timing_report(void)
{
    timing_writer_t writer = {
        .uart = SOC_DEBUG_UART,
        .deadline = uart_deadline(),
    };
    return snn_head_profile_write_report(
        timing_write, &writer, snn_head_demo_timer_hz,
        snn_head_demo_last_cycles, snn_head_uart_timing_get());
}
#endif

static int uart_write_bytes(void *ctx, const uint8_t *data, size_t length,
                            uint64_t deadline)
{
    UART_TypeDef *const uart = ctx;

    for (size_t index = 0U; index < length; ++index) {
        while ((uart->TXFIFO & UART_TXFIFO_FULL) != 0U) {
            if (__get_rv_cycle() >= deadline) {
                return SNN_HEAD_UART_ERR_TIMEOUT;
            }
        }
        uart->TXFIFO = data[index];
    }
    return SNN_HEAD_UART_OK;
}

static bool run_complete_chain(void *ctx, const float *input, float *action,
                               uint64_t *cycles)
{
    (void)ctx;
#if SNN_HEAD_TIMING
    /* RESULT cycles cover model execution only, never UART or report output. */
    const uint64_t start_cycles = __get_rv_cycle();
#endif
    const bool success = snn_head_run_chunk(input, action);
#if SNN_HEAD_TIMING
    const uint64_t end_cycles = __get_rv_cycle();

    snn_head_demo_last_cycles = end_cycles - start_cycles;
    snn_head_demo_timer_hz = (uint32_t)SystemCoreClock;
#if RV_DEBUG_ENABLE_LOGGING
    snn_head_demo_success = success;
#endif
    *cycles = snn_head_demo_last_cycles;
#else
#if RV_DEBUG_ENABLE_LOGGING
    snn_head_demo_success = success;
#endif
    *cycles = 0U;
#endif

    return success;
}

int main(void)
{
    const snn_head_uart_io_t io = {
        .read = uart_read_bytes,
        .write = uart_write_bytes,
        .now = uart_now,
        .hz = read_hz,
        .ctx = SOC_DEBUG_UART,
    };

    (void)uart_init(SOC_DEBUG_UART, SOC_DEBUG_UART_BAUDRATE);
#if RV_DEBUG_ENABLE_LOGGING
    rv_debug_set_sink(capture_debug_error, NULL);
    rv_debug_set_level(RV_DEBUG_ERROR);
#endif
#if NUCLEI_BANNER == 1
    static const uint8_t ready[] = "SNN_HEAD_UART_READY\r\n";
    for (size_t index = 0U; index < sizeof(ready) - 1U; ++index) {
        (void)uart_write(SOC_DEBUG_UART, ready[index]);
    }
#endif
    for (;;) {
#if RV_DEBUG_ENABLE_LOGGING
        snn_head_demo_success = false;
        snn_head_debug_event_valid = false;
        snn_head_debug_event[0] = '\0';
#endif
        const int status = snn_head_uart_process_one(
            &io, input_buffer, action_buffer, result_wire, run_complete_chain,
            NULL, SNN_HEAD_UART_IO_TIMEOUT_MS);
#if RV_DEBUG_ENABLE_LOGGING
        snn_head_demo_last_uart_status = status;
        if (has_terminal_frame(status)) {
            write_debug_summary(status);
        }
#else
        (void)status;
#endif
#if SNN_HEAD_TIMING
        if (status == SNN_HEAD_UART_OK) {
            (void)log_timing_report();
        }
#endif
#if RV_DEBUG_ENABLE_LOGGING || SNN_HEAD_TIMING
        if (status != SNN_HEAD_UART_ERR_TIMEOUT) {
            for (;;) {
                __WFI();
            }
        }
#endif
    }
}
