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

#if SNN_HEAD_RESIDENT || defined(SNN_HEAD_MAIN_FORMAT_TEST)
typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool ok;
} resident_record_writer_t;

static void resident_record_write(resident_record_writer_t *writer,
                                  const char *data, size_t length)
{
    if (!writer->ok) {
        return;
    }
    if (length >= writer->capacity - writer->length) {
        writer->ok = false;
        return;
    }
    memcpy(writer->data + writer->length, data, length);
    writer->length += length;
}

static void resident_record_text(resident_record_writer_t *writer,
                                 const char *text)
{
    resident_record_write(writer, text, strlen(text));
}

static void resident_record_u64(resident_record_writer_t *writer,
                                uint64_t value)
{
    char digits[20U];
    size_t first = sizeof(digits);
    do {
        digits[--first] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    resident_record_write(writer, digits + first, sizeof(digits) - first);
}

static void resident_record_hex32(resident_record_writer_t *writer,
                                  uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char encoded[8U];
    for (size_t index = 0U; index < sizeof(encoded); ++index) {
        const unsigned shift = (unsigned)(28U - 4U * index);
        encoded[index] = digits[(value >> shift) & 0xfU];
    }
    resident_record_write(writer, encoded, sizeof(encoded));
}

static void resident_record_field(resident_record_writer_t *writer,
                                  const char *name, uint64_t value)
{
    resident_record_text(writer, " ");
    resident_record_text(writer, name);
    resident_record_text(writer, "=");
    resident_record_u64(writer, value);
}

static size_t resident_record_finish(resident_record_writer_t *writer)
{
    if (!writer->ok) {
        return 0U;
    }
    writer->data[writer->length] = '\0';
    return writer->length;
}

static size_t format_resident_init(char *buffer, size_t capacity,
                                   const char *status, uint64_t cycles,
                                   uint32_t hz, uint32_t config_frames)
{
    resident_record_writer_t writer = {
        .data = buffer,
        .capacity = capacity,
        .length = 0U,
        .ok = buffer != NULL && capacity != 0U,
    };
    resident_record_text(&writer, "SNN_HEAD_RESIDENT_INIT status=");
    resident_record_text(&writer, status);
    resident_record_field(&writer, "cycles", cycles);
    resident_record_field(&writer, "hz", hz);
    resident_record_field(&writer, "config_frames", config_frames);
    resident_record_text(&writer, "\r\n");
    return resident_record_finish(&writer);
}

static size_t format_residency_audit(
    char *buffer, size_t capacity, const char *phase, const char *status,
    uint64_t cycles, uint32_t windows, uint64_t bytes, uint32_t layer,
    uint32_t query, uint32_t frame, uint32_t expected_high,
    uint32_t expected_low, uint32_t actual_high, uint32_t actual_low)
{
    resident_record_writer_t writer = {
        .data = buffer,
        .capacity = capacity,
        .length = 0U,
        .ok = buffer != NULL && capacity != 0U,
    };
    resident_record_text(&writer, "SNN_HEAD_RESIDENT_AUDIT phase=");
    resident_record_text(&writer, phase);
    resident_record_text(&writer, " status=");
    resident_record_text(&writer, status);
    resident_record_field(&writer, "cycles", cycles);
    resident_record_field(&writer, "windows", windows);
    resident_record_field(&writer, "bytes", bytes);
    resident_record_field(&writer, "layer", layer);
    resident_record_field(&writer, "query", query);
    resident_record_field(&writer, "frame", frame);
    resident_record_text(&writer, " expected=");
    resident_record_hex32(&writer, expected_high);
    resident_record_hex32(&writer, expected_low);
    resident_record_text(&writer, " actual=");
    resident_record_hex32(&writer, actual_high);
    resident_record_hex32(&writer, actual_low);
    resident_record_text(&writer, "\r\n");
    return resident_record_finish(&writer);
}
#endif

#if defined(SNN_HEAD_MAIN_FORMAT_TEST)
size_t snn_head_test_format_resident_init(
    char *buffer, size_t capacity, const char *status, uint64_t cycles,
    uint32_t hz, uint32_t config_frames)
{
    return format_resident_init(buffer, capacity, status, cycles, hz,
                                config_frames);
}

size_t snn_head_test_format_residency_audit(
    char *buffer, size_t capacity, const char *phase, const char *status,
    uint64_t cycles, uint32_t windows, uint64_t bytes, uint32_t layer,
    uint32_t query, uint32_t frame, uint32_t expected_high,
    uint32_t expected_low, uint32_t actual_high, uint32_t actual_low)
{
    return format_residency_audit(
        buffer, capacity, phase, status, cycles, windows, bytes, layer, query,
        frame, expected_high, expected_low, actual_high, actual_low);
}
#else
#include "debug.h"
#include "evalsoc_uart.h"
#include "nuclei_sdk_hal.h"
#include "nuclei_sdk_soc.h"
#include "snn_head.h"
#include "snn_head_profile.h"
#include "snn_head_uart.h"

#if SNN_HEAD_RESIDENT
static void write_resident_record(const char *data, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        (void)uart_write(SOC_DEBUG_UART, (uint8_t)data[index]);
    }
}

static void write_formatted_resident_record(const char *data, size_t length)
{
    if (length == 0U) {
        static const char failure[] =
            "SNN_HEAD_RESIDENT_RECORD status=fail\r\n";
        write_resident_record(failure, sizeof(failure) - 1U);
        for (;;) {
            __WFI();
        }
    }
    write_resident_record(data, length);
}
#endif

#if SNN_HEAD_AUDIT
#include "snn_head_audit.h"

static void report_residency_audit(const char *phase)
{
    const snn_head_audit_stats_t *const audit = snn_head_audit_stats();
    char line[320U];
    const size_t length = format_residency_audit(
        line, sizeof(line), phase, audit->passed ? "ok" : "fail",
        audit->cycles, audit->windows, (uint64_t)audit->compared_frames * 8U,
        audit->failed_layer, audit->failed_query, audit->failed_frame,
        audit->expected_high, audit->expected_low, audit->actual_high,
        audit->actual_low);
    write_formatted_resident_record(line, length);
}
#endif

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
#if SNN_HEAD_RESIDENT
    const bool initialized = snn_head_initialize();
    const snn_head_initialization_stats_t *const init =
        snn_head_initialization_stats();
    char init_line[192U];
    const size_t init_length = format_resident_init(
        init_line, sizeof(init_line), initialized ? "ok" : "fail",
        init->total_cycles, (uint32_t)SystemCoreClock, init->config_frames);
    write_formatted_resident_record(init_line, init_length);
#if SNN_HEAD_AUDIT
    static const char audit_plan_prefix[] =
        "SNN_HEAD_RESIDENT_AUDIT_PLAN sha256=";
    static const char line_end[] = "\r\n";
    const char *const audit_identity = snn_head_audit_identity();
    write_resident_record(audit_plan_prefix, sizeof(audit_plan_prefix) - 1U);
    write_resident_record(audit_identity, strlen(audit_identity));
    write_resident_record(line_end, sizeof(line_end) - 1U);
    report_residency_audit("before_init");
#endif
    if (!initialized) {
        for (;;) {
            __WFI();
        }
    }
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
#if SNN_HEAD_AUDIT
        if (status == SNN_HEAD_UART_OK) {
            const bool verified = snn_head_verify_residency();
            report_residency_audit("after_chunk_weights");
            if (!verified) {
                for (;;) {
                    __WFI();
                }
            }
        }
#endif
#if (RV_DEBUG_ENABLE_LOGGING || SNN_HEAD_TIMING) && !SNN_HEAD_RESIDENT
        if (status != SNN_HEAD_UART_ERR_TIMEOUT) {
            for (;;) {
                __WFI();
            }
        }
#endif
#if SNN_HEAD_RESIDENT
        if (status == SNN_HEAD_UART_ERR_INFERENCE) {
            for (;;) {
                __WFI();
            }
        }
#endif
    }
}
#endif
