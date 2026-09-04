#include <stdbool.h>
#include <stdint.h>

#include "debug.h"
#include "evalsoc_noc.h"
#include "evalsoc_uart.h"
#include "nuclei_sdk_soc.h"
#include "ringbuf.h"

#if defined(RELAY_CONFIG_FRAME_HEADER)
#include RELAY_CONFIG_FRAME_HEADER
#endif

#ifndef RELAY_HOST_HIGH_FIRST
#error "RELAY_HOST_HIGH_FIRST must be defined by the relay Makefile"
#endif

#define RELAY_FRAME_BYTES 8U
#define UART_RX_STORAGE_SIZE 4096U
#define UART_FIFO_WATERMARK 1U
#define RELAY_NOC_IRQ_LEVEL 1U
#define RELAY_NOC_IRQ_PRIORITY 0U

typedef struct relay_state {
    uint8_t uart_rx_storage[UART_RX_STORAGE_SIZE];
    rv_ringbuf_t uart_rx_buffer;
    volatile uint8_t timestep_finished;
    volatile uint8_t uart_rx_overflow;
    volatile uint8_t noc_error;
} relay_state_t;

static relay_state_t g_relay;

static uint32_t decode_be_u32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static void encode_be_u32(uint32_t value, uint8_t *bytes)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

/** Convert the host wire order into the logical NoC frame representation. */
static void decode_host_frame(const uint8_t bytes[RELAY_FRAME_BYTES],
                              uint32_t *high, uint32_t *low)
{
#if RELAY_HOST_HIGH_FIRST
    *high = decode_be_u32(&bytes[0]);
    *low = decode_be_u32(&bytes[4]);
#else
    *low = decode_be_u32(&bytes[0]);
    *high = decode_be_u32(&bytes[4]);
#endif
}

/** Convert a logical NoC frame into the configured host wire order. */
static void encode_host_frame(uint32_t high, uint32_t low,
                              uint8_t bytes[RELAY_FRAME_BYTES])
{
#if RELAY_HOST_HIGH_FIRST
    encode_be_u32(high, &bytes[0]);
    encode_be_u32(low, &bytes[4]);
#else
    encode_be_u32(low, &bytes[0]);
    encode_be_u32(high, &bytes[4]);
#endif
}

static void buffer_uart_byte(uint8_t byte, void *ctx)
{
    relay_state_t *const state = (relay_state_t *)ctx;
    if (rv_ringbuf_put(&state->uart_rx_buffer, byte) != RV_RINGBUF_OK) {
        state->uart_rx_overflow = 1U;
    }
}

/* Emit one logical frame in the selected host byte order. */
static void uart_write_frame(uint32_t high, uint32_t low)
{
    uint8_t bytes[RELAY_FRAME_BYTES];
    encode_host_frame(high, low, bytes);
    for (uint32_t i = 0U; i < RELAY_FRAME_BYTES; ++i) {
        (void)uart_write(UART0, bytes[i]);
    }
}

static bool is_sync_frame(uint32_t high)
{
    return ((high >> 28) & 0xFU) == 0xCU;
}

static bool is_complete_frame(uint32_t high)
{
    return ((high >> 28) & 0xFU) == 0xEU;
}

static bool is_test_response_header(uint32_t high, uint32_t low)
{
    return (((high >> 30) & 0x3U) == 0x1U) && (((low >> 23) & 0x1U) == 0U);
}

void UART0_IRQHandler(void)
{
    SAVE_IRQ_CSR_CONTEXT();

    if ((uart_get_status(UART0) & UART_IP_RXWM) != 0) {
        (void)uart_drain_rx_fifo(UART0, buffer_uart_byte, &g_relay);
    }

    RESTORE_IRQ_CSR_CONTEXT();
}

/* Drain one NoC response and forward it without adding protocol text. */
void paicore_noc_handler(void)
{
    SAVE_IRQ_CSR_CONTEXT();

    noc_irq_ack();
    noc_irq_disable();

    bool read_finish = false;
    bool test_response_active = false;
    uint32_t frames_remaining = 0U;

    while (!read_finish) {
        uint32_t high = 0U;
        uint32_t low = 0U;

        if (noc_fifo_read_frame_words(&high, &low) != 0) {
            g_relay.noc_error = 1U;
            break;
        }
        uart_write_frame(high, low);

        if (is_complete_frame(high)) {
            read_finish = true;
        }

        if (is_test_response_header(high, low)) {
            test_response_active = true;
            frames_remaining = low & 0x3FFFU;
        }

        if (test_response_active) {
            if (frames_remaining == 0U) {
                read_finish = true;
            } else {
                --frames_remaining;
            }
        }
    }

    g_relay.timestep_finished = 1U;
    noc_irq_ack();
    noc_irq_enable();

    RESTORE_IRQ_CSR_CONTEXT();
}

/* Load optional static configuration before accepting host frames. */
static void send_config_frames(void)
{
#if defined(HAS_CONFIG_FRAME)
    noc_lock_state_t state;
    const uint32_t word_count =
        (uint32_t)(sizeof(config_frame) / sizeof(config_frame[0]));

    _Static_assert((sizeof(config_frame) / sizeof(config_frame[0])) % 2U == 0U,
                   "config_frame must contain high/low word pairs");

    noc_irq_disable();
    noc_enter_critical(&state);
    for (uint32_t i = 0U; i < word_count; i += 2U) {
        noc_fifo_write_frame_words_unlocked(config_frame[i],
                                            config_frame[i + 1U]);
    }
    noc_exit_critical(&state);
    noc_irq_enable();
#endif
}

/* Initialize the UART/NoC interrupt paths used by the transparent bridge. */
static int relay_init(void)
{
    if (rv_ringbuf_init(&g_relay.uart_rx_buffer, g_relay.uart_rx_storage,
                        sizeof(g_relay.uart_rx_storage)) != RV_RINGBUF_OK) {
        RV_DEBUG_LOGE("relay", "failed to initialize UART RX ring buffer");
        return -1;
    }
    if (uart_set_rx_watermark(UART0, UART_FIFO_WATERMARK) != 0 ||
        uart_enable_rxint(UART0) != 0) {
        RV_DEBUG_LOGE("relay", "failed to initialize UART RX interrupt");
        return -1;
    }

    if (ECLIC_Register_IRQ(UART0_IRQn, ECLIC_NON_VECTOR_INTERRUPT,
                           ECLIC_LEVEL_TRIGGER, 2U, 0U,
                           UART0_IRQHandler) != 0) {
        RV_DEBUG_LOGE("relay", "failed to register UART0 interrupt");
        return -1;
    }
    if (ECLIC_Register_IRQ(PAICORE_NOC_IRQn, ECLIC_NON_VECTOR_INTERRUPT,
                           ECLIC_LEVEL_TRIGGER, RELAY_NOC_IRQ_LEVEL,
                           RELAY_NOC_IRQ_PRIORITY, paicore_noc_handler) != 0) {
        RV_DEBUG_LOGE("relay", "failed to register PAICORE NoC interrupt");
        return -1;
    }
    __enable_irq();
    return 0;
}

int main(void)
{
    uint8_t frame_bytes[RELAY_FRAME_BYTES];
    uint32_t frame_byte_index = 0U;

#if RV_DEBUG_ENABLE_LOGGING
    rv_debug_set_level(RV_DEBUG_INFO);
#if RELAY_HOST_HIGH_FIRST
    RV_DEBUG_LOGI("relay", "relay host word order: High first");
#else
    RV_DEBUG_LOGI("relay", "relay host word order: Low first");
#endif
#endif

    if (relay_init() != 0) {
        return -1;
    }

    send_config_frames();

    while (1) {
        uint8_t byte;
        if (rv_ringbuf_get(&g_relay.uart_rx_buffer, &byte) != RV_RINGBUF_OK) {
            if (g_relay.uart_rx_overflow != 0U) {
                RV_DEBUG_LOGE("relay", "UART RX ring buffer overflow");
                g_relay.uart_rx_overflow = 0U;
            }
            continue;
        }

        frame_bytes[frame_byte_index++] = byte;
        if (frame_byte_index != RELAY_FRAME_BYTES) {
            continue;
        }

        uint32_t high = 0U;
        uint32_t low = 0U;
        decode_host_frame(frame_bytes, &high, &low);
        const bool sync = is_sync_frame(high);

        if (sync) {
            g_relay.timestep_finished = 0U;
            g_relay.noc_error = 0U;
        }
        noc_fifo_write_frame_words(high, low);
        if (sync) {
            while (g_relay.timestep_finished == 0U) {
            }
            if (g_relay.noc_error != 0U) {
                RV_DEBUG_LOGE("relay", "NoC frame read failed during SYNC");
                g_relay.noc_error = 0U;
            }
            g_relay.timestep_finished = 0U;
        }

        frame_byte_index = 0U;
    }
}
