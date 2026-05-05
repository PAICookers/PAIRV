#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frames.h"
#include "nuclei_sdk_soc.h"
#include "paicore_noc.h"
#include "ringbuf.h"
#include "uart_app.h"

#define UART_RX_BUF_CAPACITY        512U
#define UART_RX_BUF_STORAGE_SIZE    (UART_RX_BUF_CAPACITY + 1U)
#define UART_FIFO_WATERMARK         1U

typedef struct uart_app {
    uint8_t rx_storage[UART_RX_BUF_STORAGE_SIZE];
    rv_ringbuf_t rx_buf;
    volatile uint32_t rx_frame_count;
    volatile uint8_t rx_data_ready;
    volatile uint32_t expected_rx_frames;
    rv_counter_t compute_start_cycles;
    rv_counter_t compute_end_cycles;
} uart_app_t;

static uart_app_t g_uart_app;

static void uart_app_print_binary_64(uint32_t high, uint32_t low)
{
    for (int i = 31; i >= 0; --i) {
        printf("%u", (high >> i) & 1U);
    }

    for (int i = 31; i >= 0; --i) {
        printf("%u", (low >> i) & 1U);
    }
    printf(" \n");
}

static int uart_app_parse_u64(const char *str, uint32_t *high, uint32_t *low)
{
    char *endptr;

    if (strncmp(str, "0b", 2) == 0 || strncmp(str, "0B", 2) == 0) {
        *high = 0U;
        *low = 0U;

        int bit_count = 0;
        for (int i = 2; str[i] != '\0'; ++i) {
            if (str[i] == '0' || str[i] == '1') {
                if (bit_count < 32) {
                    *low = (*low << 1) | (uint32_t)(str[i] - '0');
                } else if (bit_count < 64) {
                    *high = (*high << 1) | (uint32_t)(str[i] - '0');
                } else {
                    return -1;
                }
                bit_count++;
            } else if (str[i] != ' ' && str[i] != '_') {
                return -1;
            }
        }

        if (bit_count < 64) {
            if (bit_count > 32) {
                *high =
                    (*high << (64 - bit_count)) | (*low >> (bit_count - 32));
                *low = *low << (64 - bit_count) >> (64 - bit_count);
            } else {
                *low <<= (32 - bit_count);
            }
        }
        return 0;
    }

    if (strncmp(str, "0x", 2) == 0 || strncmp(str, "0X", 2) == 0) {
        unsigned long long value = strtoull(str, &endptr, 16);
        if (*endptr != '\0' && *endptr != ' ') {
            return -1;
        }
        *high = (uint32_t)(value >> 32);
        *low = (uint32_t)(value & 0xFFFFFFFFu);
        return 0;
    }

    unsigned long long value = strtoull(str, &endptr, 10);
    if (*endptr != '\0' && *endptr != ' ') {
        return -1;
    }
    *high = (uint32_t)(value >> 32);
    *low = (uint32_t)(value & 0xFFFFFFFFu);
    return 0;
}

void UART0_IRQHandler(void)
{
    SAVE_IRQ_CSR_CONTEXT();

    /* Drain every buffered UART RX byte into the app ring buffer so the shell
     * logic in the main loop can parse complete commands later. */
    if (uart_rx_irq_pending(UART0)) {
        (void)uart_drain_rx_fifo(UART0, rv_ringbuf_put_byte_cb,
                                 &g_uart_app.rx_buf);
    }

    RESTORE_IRQ_CSR_CONTEXT();
}

static void uart_app_noc_frame_handler(const rv_paicore_noc_frame_t *frame,
                                       rv_paicore_noc_irq_state_t *irq_state,
                                       void *ctx)
{
    uart_app_t *app = (uart_app_t *)ctx;

    app->rx_frame_count = frame->index;

    if (frame->index == 1U) {
        app->compute_end_cycles = __get_rv_cycle();
        printf("\n[SNN] ENTER INTERRUPT\n");
    }

    /* Two receive modes exist in this test app:
     * - wait for a finish frame after config/control traffic
     * - wait for an exact frame count after a user-triggered test frame */
    if (app->expected_rx_frames == 0U) {
        if (((frame->high >> 28) & 0xFU) == 0xEU) {
            irq_state->finish_count = 1U;
            irq_state->data_ready = true;
            irq_state->stop = true;
        }
    } else if (frame->index >= app->expected_rx_frames) {
        app->expected_rx_frames = 0U;
        irq_state->data_ready = true;
        irq_state->stop = true;
    }

    printf("[SNN] Data[%u]: ", app->rx_frame_count);
    uart_app_print_binary_64(frame->high, frame->low);

    if (irq_state->stop) {
        app->rx_data_ready = irq_state->data_ready ? 1U : 0U;
        printf("[SNN] Read %u 64-bit data words\n", app->rx_frame_count);
        printf("[SNN] EXIT INTERRUPT\n");
    }
}

int uart_app_poll_command(char *buffer, int buffer_size)
{
    static char cmd_buf[UART_APP_CMD_BUF_SIZE];
    static int cmd_idx = 0;

    /* The IRQ only stores bytes. Command framing, backspace handling, and echo
     * stay in thread context so the shell flow remains deterministic. */
    uint8_t data;
    while (rv_ringbuf_get(&g_uart_app.rx_buf, &data) == 0) {
        if (data == '\r' || data == '\n') {
            if (cmd_idx > 0) {
                int result = cmd_idx;

                cmd_buf[cmd_idx] = '\0';
                strncpy(buffer, cmd_buf, (size_t)buffer_size - 1U);
                buffer[buffer_size - 1] = '\0';
                cmd_idx = 0;
                printf(" \n");
                return result;
            }
        } else if (data == '\b' || data == 0x7FU || data == 0x08U) {
            if (cmd_idx > 0) {
                cmd_idx--;
                printf("\b \b");
            }
        } else if (data == 0x03U) {
            printf("^C\n> ");
            cmd_idx = 0;
        } else if (data >= 32U && data <= 126U) {
            if (cmd_idx < UART_APP_CMD_BUF_SIZE - 1) {
                cmd_buf[cmd_idx++] = (char)data;
                printf("%c", data);
            }
        }
    }

    return -1;
}

static inline void uart_app_write_frame(uint32_t hi32, uint32_t lo32)
{
    noc_fifo_write_frame_words(hi32, lo32);
}

static void uart_app_print_help(void)
{
    printf("\n===== Available Commands =====\n");
    printf("help                     - Show this help message\n");
    printf("status                   - Show system status\n");
    printf("clear                    - Clear UART buffer\n");
    printf("\nSNN Configuration:\n");
    printf("snn config               - Send configuration data to SNN\n");
    printf("snn sync                 - Send predefined sync frame to SNN\n");
    printf("snn status               - Show SNN status\n");
    printf("snn test                 - Test SNN data output format\n");
    printf("\nUser Input:\n");
    printf("user <64-bit-value>      - Send 64-bit value directly to SNN\n");
    printf("user help                - Show user input examples\n");
    printf("================================\n");
}

static void uart_app_print_user_help(void)
{
    printf("\n===== User Input Examples =====\n");
    printf("Send a 64-bit value directly to SNN:\n");
    printf("  user 0x123456789ABCDEF0\n");
    printf(
        "  user "
        "0b1010101010101010101010101010101010101010101010101010101010101010\n");
    printf("  user 12297829382473034410\n");
    printf("\nNote: The value will be sent immediately to SNN.\n");
    printf("================================\n");
}

static void uart_app_print_status(void)
{
    /* Keep the original smoke-test status dump shape so a host operator can
     * quickly confirm clock, shell backlog, and NoC receive state. */
    printf("\n===== System Status =====\n");
    printf("CPU Frequency: %lu Hz\n", SystemCoreClock);
    printf("UART Baudrate: %u bps\n", SOC_DEBUG_UART_BAUDRATE);
    printf("UART RX Buffer: %lu/%lu bytes\n",
           (unsigned long)rv_ringbuf_available(&g_uart_app.rx_buf),
           (unsigned long)UART_RX_BUF_CAPACITY);
    printf("SNN Data Ready: %s\n", g_uart_app.rx_data_ready ? "YES" : "NO");
    printf("SNN Data Count: %u\n", g_uart_app.rx_frame_count);
    printf("SNN IRQ Enabled: %s\n", noc_irq_is_enabled() ? "YES" : "NO");
    printf("Config Frame Size: %u 32-bit words\n", CONFIG_FRAME_WORDS);
    printf("Sync Frame Size: %u 32-bit words\n", SYNC_FRAME_WORDS);
    printf("===========================\n");
}

static void uart_app_send_config(void)
{
    noc_lock_state_t tx_state;
    uint32_t total_64bit = CONFIG_FRAME_WORDS / 2U;
    rv_counter_t start_cycles;
    rv_counter_t cycles;

    printf("\nSending configuration data to SNN...\n");
    printf("Total 64-bit data to send: %u\n", total_64bit);

    start_cycles = __get_rv_cycle();
    noc_irq_disable();
    noc_enter_critical(&tx_state);

    for (uint32_t i = 0; i < CONFIG_FRAME_WORDS; i += 2U) {
        noc_fifo_write_frame_words_unlocked(config_frame[i],
                                            config_frame[i + 1U]);
    }

    noc_exit_critical(&tx_state);
    noc_irq_enable();
    cycles = __get_rv_cycle() - start_cycles;

    printf("Configuration data sent successfully.\n");

    uint32_t high = (uint32_t)(cycles >> 32);
    uint32_t low = (uint32_t)(cycles & 0xFFFFFFFFULL);
    printf("Config Time: 0x%08X%08X\n", (unsigned int)high, (unsigned int)low);
}

static void uart_app_send_sync_frame(void)
{
    noc_lock_state_t tx_state;
    uint32_t total_64bit = SYNC_FRAME_WORDS / 2U;

    printf("\nSending predefined sync frame to SNN...\n");
    printf("Total 64-bit data to send: %u\n", total_64bit);

    noc_irq_disable();
    noc_enter_critical(&tx_state);

    for (uint32_t i = 0; i < SYNC_FRAME_WORDS; i += 2U) {
        noc_fifo_write_frame_words_unlocked(sync_frame[i], sync_frame[i + 1U]);
    }

    noc_exit_critical(&tx_state);
    noc_irq_enable();
    printf("sync frame sent successfully.\n");
}

static void uart_app_run_test(void)
{
    /* Reserved for future explicit receive-format probes. The current app uses
     * the `user` path plus the shared NoC callback for those checks. */
    printf("SNN test command is not implemented.\n");
}

static void uart_app_send_user_frame(int argc, char *argv[])
{
    uint32_t hi32;
    uint32_t lo32;

    if (argc < 2) {
        printf("Usage: user <64-bit-value>\n");
        printf("Examples:\n");
        printf("  user 0x123456789ABCDEF0\n");
        printf("  user "
               "0b1010101010101010101010101010101010101010101010101010101010101"
               "010\n");
        printf("  user 12297829382473034410\n");
        printf("\nFor more examples, type 'user help'\n");
        return;
    }

    if (strcmp(argv[1], "help") == 0) {
        uart_app_print_user_help();
        return;
    }

    if (uart_app_parse_u64(argv[1], &hi32, &lo32) != 0) {
        printf("ERROR: Invalid format for 64-bit value: %s\n", argv[1]);
        printf("Valid formats:\n");
        printf("  Binary: 0b1010...\n");
        printf("  Hex: 0x123456789ABCDEF0\n");
        printf("  Decimal: 12345678901234567890\n");
        return;
    }

    if (g_uart_app.expected_rx_frames == 0U) {
        uint8_t highest_two_bits = (uint8_t)((lo32 >> 30) & 0x03U);
        uint16_t low_14bits = (uint16_t)(hi32 & 0x3FFFU);

        if (highest_two_bits == 0x00U) {
            printf("  Detected config frame\n");
            g_uart_app.expected_rx_frames = 1U;
        } else if (highest_two_bits == 0x01U) {
            printf("  Detected test frame \n");
            g_uart_app.expected_rx_frames = (uint32_t)low_14bits + 1U;
        } else if (highest_two_bits == 0x02U) {
            printf("  Detected work frame\n");
        } else {
            printf("  Detected control frame\n");
        }
    }

    /* Keep the simple original test-program behavior: classify the outgoing
     * frame first, then send it and let the shared NoC handler decide when the
     * matching receive sequence is complete. */
    printf("\nSending 64-bit user input to SNN...\n");
    noc_irq_disable();
    g_uart_app.compute_start_cycles = __get_rv_cycle();
    uart_app_write_frame(hi32, lo32);
    noc_irq_enable();
}

void uart_app_process_command(const char *cmd)
{
    char cmd_copy[UART_APP_CMD_BUF_SIZE];
    char *argv[10];
    int argc = 0;
    char *token;

    /* Parse one shell line into argv-style tokens, then dispatch to the same
     * high-level test actions as the original monolithic UART demo. */
    if (strlen(cmd) == 0U) {
        return;
    }

    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1U);
    cmd_copy[sizeof(cmd_copy) - 1U] = '\0';

    token = strtok(cmd_copy, " ");
    while (token != NULL && argc < 10) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    if (argc == 0) {
        return;
    }

    if (strcmp(argv[0], "help") == 0) {
        uart_app_print_help();
    } else if (strcmp(argv[0], "status") == 0) {
        uart_app_print_status();
    } else if (strcmp(argv[0], "clear") == 0) {
        rv_ringbuf_reset(&g_uart_app.rx_buf);
        printf("UART buffer cleared.\n");
    } else if (strcmp(argv[0], "snn") == 0 && argc > 1) {
        if (strcmp(argv[1], "config") == 0) {
            uart_app_send_config();
        } else if (strcmp(argv[1], "sync") == 0) {
            uart_app_send_sync_frame();
        } else if (strcmp(argv[1], "status") == 0) {
            printf("\nSNN Status:\n");
            printf("  Data Ready: %s\n",
                   g_uart_app.rx_data_ready ? "YES" : "NO");
            printf("  Data Count: %u\n", g_uart_app.rx_frame_count);
            printf("  IRQ Enabled: %s\n", noc_irq_is_enabled() ? "YES" : "NO");
        } else if (strcmp(argv[1], "test") == 0) {
            uart_app_run_test();
        } else {
            printf("Unknown SNN command: '%s'\n", argv[1]);
        }
    } else if (strcmp(argv[0], "user") == 0) {
        uart_app_send_user_frame(argc, argv);
    } else {
        printf("Unknown command: '%s'\n", cmd);
        printf("Type 'help' for available commands.\n");
    }
}

int uart_app_init(void)
{
    int32_t result;

    /* Initialize the shell RX path before enabling UART interrupts so the
     * first received byte always has valid storage. */
    result = rv_ringbuf_init(&g_uart_app.rx_buf, g_uart_app.rx_storage,
                             sizeof(g_uart_app.rx_storage));
    if (result != RV_RINGBUF_OK) {
        printf("ERROR: Failed to initialize UART RX ring buffer.\n");
        return -1;
    }

    uart_set_rx_watermark(UART0, UART_FIFO_WATERMARK);
    uart_enable_rxint(UART0);

    result = ECLIC_Register_IRQ(UART0_IRQn, ECLIC_NON_VECTOR_INTERRUPT,
                                ECLIC_LEVEL_TRIGGER, 2, 0, UART0_IRQHandler);

    if (result != 0) {
        printf("ERROR: Failed to register UART0 interrupt (code: %ld)\n",
               (long)result);
        return -1;
    }

    /* Register the shared PAICORE NoC handler after the UART shell path so the
     * app can both accept commands and observe returned NoC frames. */
    rv_paicore_noc_set_frame_handler(uart_app_noc_frame_handler, &g_uart_app);
    result = rv_paicore_noc_register_irq(ECLIC_NON_VECTOR_INTERRUPT,
                                         ECLIC_LEVEL_TRIGGER, 1, 0);

    if (result != 0) {
        printf("ERROR: Failed to register NoC interrupt (code: %ld)\n",
               (long)result);
        return -1;
    }

    __enable_irq();

    g_uart_app.rx_frame_count = 0U;
    g_uart_app.rx_data_ready = 0U;
    g_uart_app.expected_rx_frames = 0U;
    g_uart_app.compute_start_cycles = 0U;
    g_uart_app.compute_end_cycles = 0U;

    return 0;
}

int uart_app_service_received_data(void)
{
    if (!g_uart_app.rx_data_ready) {
        return 0;
    }

    /* The test app currently just acknowledges the receive milestone and
     * prints the measured send->receive cycle span. */
    printf("\n[SNN] Processing received data...\n");
    g_uart_app.rx_data_ready = 0U;
    printf("[SNN] Data processing complete.\n");
    printf("Thread Compute Time: %llu cycles\n",
           (unsigned long long)(g_uart_app.compute_end_cycles -
                                g_uart_app.compute_start_cycles));
    return 1;
}

void uart_app_print_prompt(void)
{
    static uint8_t prompt_printed = 0U;

    /* Print the one-time app banner here so `main()` only has to manage the
     * loop shape and prompt timing. */
    if (!prompt_printed) {
        printf("UART app ready. Type 'help'.\n");
        prompt_printed = 1U;
    }

    printf("> ");
}
