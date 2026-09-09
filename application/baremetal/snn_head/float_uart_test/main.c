/*
 * Standalone board test for the production SNN Head float UART boundary.
 * It links the real protocol implementation but no model/runtime artifacts,
 * making framing, float conversion, CRC, and finite-value failures observable
 * without a PAICore inference.
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "evalsoc_uart.h"
#include "nuclei_sdk_hal.h"
#include "nuclei_sdk_soc.h"
#include "snn_head_uart.h"

#ifndef SNN_HEAD_UART_IO_TIMEOUT_MS
#define SNN_HEAD_UART_IO_TIMEOUT_MS 1000U
#endif

static float input_buffer[SNN_HEAD_INPUT_FLOAT_COUNT];
static float action_buffer[SNN_HEAD_ACTION_FLOAT_COUNT];
static uint8_t result_wire[SNN_HEAD_UART_RESULT_PAYLOAD_BYTES];

static uint64_t uart_now(void *ctx)
{
    (void)ctx;
    return __get_rv_cycle();
}

static uint32_t uart_hz(void *ctx)
{
    (void)ctx;
    return (uint32_t)SystemCoreClock;
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

static bool echo_input_prefix(void *ctx, const float *input, float *action,
                              uint64_t *cycles)
{
    (void)ctx;
    /* Preserve timestep layout: RESULT[t] echoes INPUT[t][0..6]. */
    for (uint32_t timestep = 0U; timestep < SNN_HEAD_TIMESTEPS; ++timestep) {
        memcpy(&action[timestep * SNN_HEAD_ACTION_DIM],
               &input[timestep * SNN_HEAD_INPUT_DIM],
               SNN_HEAD_ACTION_DIM * sizeof(*action));
    }
    /* Test-only trigger for the production output finite-value guard. */
    if (input[0] == 12345.0f) {
        action[0] = INFINITY;
    }
    *cycles = 0U;
    return true;
}

int main(void)
{
    const snn_head_uart_io_t io = {
        .read = uart_read_bytes,
        .write = uart_write_bytes,
        .now = uart_now,
        .hz = uart_hz,
        .ctx = SOC_DEBUG_UART,
    };

    (void)uart_init(SOC_DEBUG_UART, SOC_DEBUG_UART_BAUDRATE);
#if NUCLEI_BANNER == 1
    static const uint8_t ready[] =
        "SNN_HEAD_FLOAT_UART_TEST_ECHO_PREFIX\r\nSNN_HEAD_UART_READY\r\n";
    for (size_t index = 0U; index < sizeof(ready) - 1U; ++index) {
        (void)uart_write(SOC_DEBUG_UART, ready[index]);
    }
#endif
    for (;;) {
        (void)snn_head_uart_process_one(&io, input_buffer, action_buffer,
                                        result_wire, echo_input_prefix, NULL,
                                        SNN_HEAD_UART_IO_TIMEOUT_MS);
    }
}
