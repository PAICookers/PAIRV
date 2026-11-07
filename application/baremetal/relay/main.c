#include <stdbool.h>
#include <stdint.h>

#include "debug.h"
#include "evalsoc_noc.h"
#include "evalsoc_uart.h"
#include "nuclei_sdk_soc.h"
#include "relay_protocol.h"
#include "ringbuf.h"

#if defined(RELAY_CONFIG_FRAME_HEADER)
#include RELAY_CONFIG_FRAME_HEADER
#ifndef HAS_CONFIG_FRAME
#error "configuration header must define HAS_CONFIG_FRAME and config_frame"
#endif
#endif

#define UART_RX_STORAGE_SIZE 4096U
#define UART_FIFO_WATERMARK 1U
#define RELAY_NOC_IRQ_LEVEL 1U
#define RELAY_NOC_IRQ_PRIORITY 0U

typedef enum {
    /* Setup errors can retry here; later failures require an external reset. */
    RELAY_CONFIG_WAIT_SETUP = 0,
    RELAY_CONFIG_RECEIVING,
    RELAY_CONFIG_READY,
    RELAY_CONFIG_FAILED,
} relay_config_phase_t;

typedef struct relay_state {
    /* Only ISR-shared flags are volatile; configuration state is main-owned. */
    uint8_t uart_rx_storage[UART_RX_STORAGE_SIZE];
    rv_ringbuf_t uart_rx_buffer;
    volatile uint8_t timestep_finished;
    volatile uint8_t uart_rx_overflow;
    volatile uint8_t noc_error;
    relay_config_setup_t config;
    uint32_t config_crc;
    uint32_t config_forwarded;
    relay_config_phase_t config_phase;
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

/* Hash logical high/low words so CRC does not depend on UART word order. */
static uint32_t config_crc_update(uint32_t crc, uint32_t high, uint32_t low)
{
    uint8_t bytes[RELAY_FRAME_BYTES];
    encode_be_u32(high, bytes);
    encode_be_u32(low, bytes + 4U);
    return relay_crc32_update(crc, bytes, RELAY_FRAME_BYTES);
}

/* CONFIG_SETUP is always decoded high-first, before the app order is known. */
static bool decode_setup_frame(const uint8_t bytes[RELAY_FRAME_BYTES],
                               relay_config_setup_t *setup)
{
    return relay_decode_config_setup(decode_be_u32(bytes),
                                     decode_be_u32(bytes + 4U),
                                     setup) == RELAY_OK;
}

/* Control frames stay high-first; application frames use the configured
 * order. */
static void send_relay_frame(uint32_t high, uint32_t low, bool app_order)
{
    uint8_t bytes[RELAY_FRAME_BYTES];
    if (app_order) {
        encode_host_frame(high, low, bytes);
    } else {
        encode_be_u32(high, bytes);
        encode_be_u32(low, bytes + 4U);
    }
    for (uint32_t i = 0U; i < RELAY_FRAME_BYTES; ++i) {
        (void)uart_write(UART0, bytes[i]);
    }
}

/* ACK reports the forwarded count and finalized CRC (zero when disabled). */
static void send_config_ack(relay_error_t error)
{
    uint32_t high = 0U;
    uint32_t low = 0U;
    const uint32_t crc =
        g_relay.config.crc_enabled ? g_relay.config_crc ^ 0xFFFFFFFFU : 0U;
    relay_build_config_ack(error, g_relay.config_forwarded, crc, &high, &low);
    send_relay_frame(high, low, false);
}

static void send_relay_status(relay_error_t error, uint64_t detail,
                              bool app_order)
{
    uint32_t high = 0U;
    uint32_t low = 0U;
    relay_build_status(error, detail, &high, &low);
    send_relay_frame(high, low, app_order);
}

/* Disable producers and discard partial input before the terminal status. */
static void stop_relay(void)
{
    (void)uart_disable_rxint(UART0);
    noc_irq_disable();
    rv_ringbuf_reset(&g_relay.uart_rx_buffer);
    g_relay.timestep_finished = 0U;
    g_relay.noc_error = 0U;
    g_relay.config_phase = RELAY_CONFIG_FAILED;
}

/* Validate declarations before any NoC write. */
static relay_error_t validate_setup(const relay_config_setup_t *setup)
{
    if (setup->enable_host_config != (ENABLE_HOST_CONFIG != 0)) {
        return RELAY_MODE_MISMATCH;
    }
#if RELAY_HOST_HIGH_FIRST
    if (setup->low_first) {
        return RELAY_INVALID_FIELD;
    }
#else
    if (!setup->low_first) {
        return RELAY_INVALID_FIELD;
    }
#endif
#if defined(HAS_CONFIG_FRAME)
    if (setup->frame_count !=
        (uint32_t)(sizeof(config_frame) / sizeof(config_frame[0]) / 2U)) {
        return RELAY_COUNT_MISMATCH;
    }
#endif
    return RELAY_OK;
}

/* A CRC failure is terminal because forwarded NoC configuration cannot roll
 * back. */
static void finish_configuration(void)
{
    if (g_relay.config.crc_enabled &&
        (g_relay.config_crc ^ 0xFFFFFFFFU) != g_relay.config.expected_crc) {
        stop_relay();
        send_config_ack(RELAY_CRC_MISMATCH);
        return;
    }
    send_config_ack(RELAY_OK);
    g_relay.config_phase = RELAY_CONFIG_READY;
}

/* Configuration payload is opaque, including bytes that resemble relay
 * opcodes. */
static void forward_config_frame(uint32_t high, uint32_t low)
{
    noc_fifo_write_frame_words(high, low);
    if (g_relay.config.crc_enabled) {
        g_relay.config_crc = config_crc_update(g_relay.config_crc, high, low);
    }
    ++g_relay.config_forwarded;
    if (g_relay.config_forwarded == g_relay.config.frame_count) {
        finish_configuration();
    }
}

#if defined(HAS_CONFIG_FRAME)
/* Host setup triggers forwarding of the compiled logical high/low pairs. */
static void forward_static_config(void)
{
    _Static_assert((sizeof(config_frame) / sizeof(config_frame[0])) % 2U == 0U,
                   "config_frame must contain high/low word pairs");
    _Static_assert((sizeof(config_frame) / sizeof(config_frame[0]) / 2U) >=
                           1U &&
                       (sizeof(config_frame) / sizeof(config_frame[0]) / 2U) <=
                           RELAY_CONFIG_COUNT_MAX,
                   "config_frame count must fit CONFIG_SETUP");

    noc_lock_state_t state;
    noc_irq_disable();
    noc_enter_critical(&state);
    for (uint32_t i = 0U; i < g_relay.config.frame_count; ++i) {
        const uint32_t high = config_frame[i * 2U];
        const uint32_t low = config_frame[i * 2U + 1U];
        noc_fifo_write_frame_words_unlocked(high, low);
        if (g_relay.config.crc_enabled) {
            g_relay.config_crc =
                config_crc_update(g_relay.config_crc, high, low);
        }
        ++g_relay.config_forwarded;
    }
    noc_exit_critical(&state);
    noc_irq_enable();
    finish_configuration();
}
#endif

/* Keep WAIT_SETUP on rejected input; commit state only after validation. */
static relay_error_t accept_config_setup(const uint8_t bytes[RELAY_FRAME_BYTES])
{
    relay_config_setup_t setup;
    if (!decode_setup_frame(bytes, &setup)) {
        return RELAY_INVALID_FIELD;
    }
    const relay_error_t validation = validate_setup(&setup);
    if (validation != RELAY_OK) {
        return validation;
    }

    g_relay.config = setup;
    g_relay.config_crc = 0xFFFFFFFFU;
    g_relay.config_forwarded = 0U;
#if defined(HAS_CONFIG_FRAME)
    /* CONFIG_SETUP controls forwarding; the compiled header remains the
     * source. */
    forward_static_config();
#else
    g_relay.config_phase = RELAY_CONFIG_RECEIVING;
#endif
    return RELAY_OK;
}

static void buffer_uart_byte(uint8_t byte, void *ctx)
{
    relay_state_t *const state = (relay_state_t *)ctx;
    /* A lost byte invalidates the suffix of the stream. */
    if (state->uart_rx_overflow != 0U) {
        return;
    }
    if (rv_ringbuf_put(&state->uart_rx_buffer, byte) != RV_RINGBUF_OK) {
        state->uart_rx_overflow = 1U;
    }
}

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

/* Drain one NoC response and forward raw frames until its completion marker. */
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

    while (1) {
        /* Terminal failures leave recovery to an external reset. */
        if (g_relay.config_phase == RELAY_CONFIG_FAILED) {
            break;
        }
        if (g_relay.uart_rx_overflow != 0U) {
            const bool app_order = g_relay.config_phase == RELAY_CONFIG_READY;
            stop_relay();
            frame_byte_index = 0U;
            send_relay_status(RELAY_UART_RX_OVERFLOW, 0U, app_order);
            continue;
        }

        uint8_t byte;
        /* RX can overflow between dequeue and frame assembly. */
        if (rv_ringbuf_get(&g_relay.uart_rx_buffer, &byte) != RV_RINGBUF_OK ||
            g_relay.uart_rx_overflow != 0U) {
            continue;
        }

        frame_bytes[frame_byte_index++] = byte;
        if (frame_byte_index != RELAY_FRAME_BYTES) {
            continue;
        }

        /* The first frame selects and validates the configuration source. */
        if (g_relay.config_phase == RELAY_CONFIG_WAIT_SETUP) {
            const relay_error_t error = accept_config_setup(frame_bytes);
            if (error != RELAY_OK) {
                send_config_ack(error);
            }
            frame_byte_index = 0U;
            continue;
        }

        uint32_t high = 0U;
        uint32_t low = 0U;
        decode_host_frame(frame_bytes, &high, &low);
        if (g_relay.config_phase != RELAY_CONFIG_READY) {
            forward_config_frame(high, low);
            frame_byte_index = 0U;
            continue;
        }

        const bool sync = is_sync_frame(high);

        if (sync) {
            g_relay.timestep_finished = 0U;
            g_relay.noc_error = 0U;
        }
        noc_fifo_write_frame_words(high, low);
        if (sync) {
            while (g_relay.timestep_finished == 0U &&
                   g_relay.uart_rx_overflow == 0U) {
            }
            if (g_relay.uart_rx_overflow != 0U) {
                continue;
            }
            if (g_relay.noc_error != 0U) {
                if (g_relay.config_phase != RELAY_CONFIG_FAILED) {
                    stop_relay();
                    send_relay_status(RELAY_NOC_READ_FAILED, 0U, true);
                }
                frame_byte_index = 0U;
                continue;
            }
            g_relay.timestep_finished = 0U;
        }

        frame_byte_index = 0U;
    }

    return -1;
}
