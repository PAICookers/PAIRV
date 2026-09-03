#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "nuclei_sdk_soc.h"
#include "ringbuf.h"
#include "uart_app.h"
#include "uart_frame.h"
#include "utils.h"

#define RX_BUF_CAPACITY 512U
#define RX_BUF_STORAGE_SIZE (RX_BUF_CAPACITY + 1U)
#define FIFO_WATERMARK 1U
#define NOC_COMPLETE_KIND 0xEU
#define NOC_KIND_OFFSET 28U
#define NOC_KIND_MASK 0xFU
#define NOC_IRQ_LEVEL 1U
#define NOC_IRQ_PRIORITY 0U

#define RESPONSE_TIMEOUT_MS 2000U
#define CONFIG_SEND_TIMEOUT_MS 5000U
#define WORK_RESPONSE_TIMEOUT_MS 3000U

typedef enum timing_kind {
    TIMING_NONE = 0,
    TIMING_COMPUTE,
    TIMING_RESPONSE,
} timing_kind_t;

typedef struct uart_app {
    uint8_t shell_storage[RX_BUF_STORAGE_SIZE];
    rv_ringbuf_t shell_buf;
    volatile uint32_t rx_frame_count;
    volatile uint8_t rx_data_ready;
    volatile uint8_t response_pending;
    volatile frame_kind_t pending_kind;
    volatile uint32_t expected_rx_frames;
    volatile timing_kind_t timing_kind;
    volatile rv_counter_t start_cycles;
    volatile rv_counter_t end_cycles;
} uart_app_t;

static uart_app_t g_uart_app;
static char g_shell_command[UART_APP_CMD_BUF_SIZE];
static int g_shell_command_length;
static uint8_t g_prompt_printed;

/*
 * Serialize main-context output with the NoC ISR, which streams every
 * response frame directly to the same UART.  UART RX remains interruptible;
 * only the NoC source is gated, so shell input cannot be lost while text is
 * being emitted.
 */
static void output_critical_enter(void) { noc_irq_disable(); }

static void output_critical_exit(void) { noc_irq_enable(); }

static void buffer_uart_byte(uint8_t byte, void *ctx)
{
    (void)rv_ringbuf_put((rv_ringbuf_t *)ctx, byte);
}

static void print_cycles(const char *prefix, rv_counter_t start,
                         rv_counter_t end)
{
    const uint64_t delta = (uint64_t)end - (uint64_t)start;
    const uint32_t high = (uint32_t)(delta >> 32U);
    const uint32_t low = (uint32_t)delta;
    printf("%s0x%08lX%08lX cycles\n", prefix, (unsigned long)high,
           (unsigned long)low);
}

void UART0_IRQHandler(void)
{
    SAVE_IRQ_CSR_CONTEXT();
    /* IRQ context only drains bytes; parsing and output stay in main. */
    if ((uart_get_status(UART0) & UART_IP_RXWM) != 0) {
        (void)uart_drain_rx_fifo(UART0, buffer_uart_byte,
                                 &g_uart_app.shell_buf);
    }
    RESTORE_IRQ_CSR_CONTEXT();
}

static bool handle_noc_frame(uint32_t high, uint32_t low)
{
    const uint32_t response_kind =
        (high >> NOC_KIND_OFFSET) & NOC_KIND_MASK;
    const uint32_t index = ++g_uart_app.rx_frame_count;
    /*
     * Responses are printed at the point they leave the NoC FIFO.  A response
     * may contain an arbitrary number of frames, so retaining them for the
     * main loop would either truncate the trace or require an unbounded buffer.
     */
    printf("[SNN] Data[%lu]: 0x%08lX%08lX\n", (unsigned long)index,
           (unsigned long)high, (unsigned long)low);
    if (response_kind == NOC_COMPLETE_KIND) {
        printf("[SNN] Receive complete\n");
    }
    if (g_uart_app.expected_rx_frames != 0U &&
        index >= g_uart_app.expected_rx_frames) {
        g_uart_app.expected_rx_frames = 0U;
        return true;
    }
    return g_uart_app.expected_rx_frames == 0U &&
           response_kind == NOC_COMPLETE_KIND;
}

void paicore_noc_handler(void)
{
    bool stop = false;
    SAVE_IRQ_CSR_CONTEXT();
    noc_irq_ack();
    noc_irq_disable();
    while (!stop) {
        uint32_t high = 0U;
        uint32_t low = 0U;
        if (noc_fifo_read_frame_words(&high, &low) != 0) {
            break;
        }
        stop = handle_noc_frame(high, low);
    }
    if (stop) {
        g_uart_app.end_cycles = __get_rv_cycle();
        g_uart_app.response_pending = 0U;
        g_uart_app.rx_data_ready = 1U;
    }
    RESTORE_IRQ_CSR_CONTEXT();
    noc_irq_enable();
}

int uart_app_poll_command(char *buffer, int buffer_size)
{
    uint8_t data;
    int result = -1;

    output_critical_enter();
    while (rv_ringbuf_get(&g_uart_app.shell_buf, &data) == RV_RINGBUF_OK) {
        if (data == '\r' || data == '\n') {
            if (g_shell_command_length > 0) {
                const int length = g_shell_command_length;
                g_shell_command[length] = '\0';
                strncpy(buffer, g_shell_command, (size_t)buffer_size - 1U);
                buffer[buffer_size - 1] = '\0';
                g_shell_command_length = 0;
                printf(" \n");
                result = length;
                break;
            }
        } else if (data == '\b' || data == 0x7FU || data == 0x08U) {
            if (g_shell_command_length > 0) {
                --g_shell_command_length;
                printf("\b \b");
            }
        } else if (data == 0x03U) {
            g_shell_command_length = 0;
            printf("^C\n> ");
        } else if (data >= 32U && data <= 126U) {
            if (g_shell_command_length < UART_APP_CMD_BUF_SIZE - 1) {
                g_shell_command[g_shell_command_length++] = (char)data;
                printf("%c", data);
            }
        }
    }
    output_critical_exit();
    return result;
}

static uint32_t response_timeout_ms(frame_kind_t kind)
{
    if (kind == FRAME_WORK) {
        return WORK_RESPONSE_TIMEOUT_MS;
    }
    return RESPONSE_TIMEOUT_MS;
}

static rv_counter_t timeout_cycles(uint32_t timeout_ms)
{
    return ((rv_counter_t)(SystemCoreClock / 1000U)) * (rv_counter_t)timeout_ms;
}

static bool response_timed_out(void)
{
    const rv_counter_t elapsed = __get_rv_cycle() - g_uart_app.start_cycles;
    const rv_counter_t limit =
        timeout_cycles(response_timeout_ms(g_uart_app.pending_kind));
    return g_uart_app.response_pending != 0U && elapsed > limit;
}

static void release_timed_out_response(void)
{
    if (!response_timed_out()) {
        return;
    }

    printf("ERROR: %s response timeout; receive state released.\n",
           uart_app_frame_name(g_uart_app.pending_kind));
    g_uart_app.response_pending = 0U;
    g_uart_app.expected_rx_frames = 0U;
    g_uart_app.timing_kind = TIMING_NONE;
}

static bool response_busy(frame_kind_t kind)
{
    /* Random debug traffic is intentionally unrestricted.  INIT and SYNC
     * are the only commands whose completion ordering the shell enforces. */
    if (kind != FRAME_INIT && kind != FRAME_SYNC) {
        return false;
    }
    release_timed_out_response();
    if (!g_uart_app.response_pending) {
        return g_uart_app.rx_data_ready != 0U;
    }
    if (g_uart_app.rx_data_ready) {
        return true;
    }
    /* SYNC is the normal command that closes a pending WORK response. */
    if (g_uart_app.pending_kind == FRAME_WORK && kind == FRAME_SYNC) {
        return false;
    }
    return true;
}

static void begin_response(frame_kind_t kind, uint32_t expected,
                           timing_kind_t timing)
{
    g_uart_app.rx_frame_count = 0U;
    g_uart_app.rx_data_ready = 0U;
    g_uart_app.expected_rx_frames = expected;
    g_uart_app.pending_kind = kind;
    g_uart_app.response_pending = 1U;
    g_uart_app.timing_kind = timing;
    g_uart_app.start_cycles = __get_rv_cycle();
    RV_DEBUG_LOGD("uart", "begin %s response expected=%lu",
                  uart_app_frame_name(kind), (unsigned long)expected);
}

static bool send_view(frame_kind_t kind, const frame_view_t *view)
{
    noc_lock_state_t tx_state;
    bool transmit_timeout = false;
    printf("Sending %s frame (%lu 64-bit words)...\n",
           uart_app_frame_name(kind), (unsigned long)(view->word_count / 2U));
    /* Start after the pre-send log; response timing must exclude printf cost.
     */
    const rv_counter_t tx_start = __get_rv_cycle();
    noc_enter_critical(&tx_state);
    for (size_t i = 0U; i < view->word_count; i += 2U) {
        noc_fifo_write_frame_words_unlocked(view->words[i],
                                            view->words[i + 1U]);
        if (kind == FRAME_CONFIG &&
            (__get_rv_cycle() - tx_start) >
                timeout_cycles(CONFIG_SEND_TIMEOUT_MS)) {
            transmit_timeout = true;
            break;
        }
    }
    if (kind != FRAME_CONFIG && g_uart_app.response_pending != 0U) {
        /* Keep the timestamp inside the NoC critical section so an immediate
         * completion IRQ cannot race the start of the wait interval. */
        g_uart_app.start_cycles = __get_rv_cycle();
    }
    noc_exit_critical(&tx_state);
    if (transmit_timeout) {
        printf("ERROR: %s transmit exceeded %u ms; frame stream stopped.\n",
               uart_app_frame_name(kind), CONFIG_SEND_TIMEOUT_MS);
        return false;
    }
    if (kind == FRAME_CONFIG) {
        printf("Configuration data sent successfully.\n");
    } else {
        printf("%s frame sent successfully.\n", uart_app_frame_name(kind));
    }
    RV_DEBUG_LOGI("uart", "%s: %lu words", uart_app_frame_name(kind),
                  (unsigned long)view->word_count);
    return true;
}

static void send_frame(frame_kind_t kind)
{
    frame_view_t view;
    const frame_status_t status = uart_app_get_frame(kind, &view);
    if (status != FRAME_AVAILABLE) {
        printf("ERROR: %s frame is %s; nothing was sent.\n",
               uart_app_frame_name(kind), uart_app_frame_status_name(status));
        return;
    }
    if (response_busy(kind)) {
        printf("ERROR: response pending; complete it before sending %s.\n",
               uart_app_frame_name(kind));
        return;
    }
    const bool work_response_in_flight = g_uart_app.response_pending != 0U &&
                                         g_uart_app.pending_kind == FRAME_WORK;
    if (kind == FRAME_TEST) {
        if (view.word_count != 2U || ((view.words[0] >> 30U) & 0x3U) != 1U) {
            printf("ERROR: test frame must be one TEST request word.\n");
            return;
        }
        const uint32_t expected = (view.words[1] & 0x3FFFU) + 1U;
        begin_response(kind, expected, TIMING_RESPONSE);
    } else if (kind != FRAME_CONFIG &&
               !(work_response_in_flight && kind == FRAME_SYNC)) {
        begin_response(kind, 0U,
                       kind == FRAME_WORK ? TIMING_COMPUTE : TIMING_RESPONSE);
    }
    (void)send_view(kind, &view);
}

static void print_help(void)
{
    printf("\n===== Available Commands =====\n"
           "help                     - Show this help message\n"
           "status                   - Show system status\n"
           "clear                    - Clear shell and receive state\n"
           "snn config/init/work/input/sync/test\n"
           "log off|error|warn|info|debug\n"
           "user <64-bit-value>      - Send one 64-bit value to SNN\n"
           "user help                - Show user input examples\n"
           "================================\n");
}

static void print_user_help(void)
{
    printf("Usage: user <64-bit-value>\n"
           "  user 0x123456789ABCDEF0\n"
           "  user 0b1010...\n"
           "  user 12345678901234567890\n");
}

static void print_status(void)
{
    printf("\n===== System Status =====\n");
    printf("CPU Frequency: %lu Hz\n", SystemCoreClock);
    printf("UART Baudrate: %u bps\n", SOC_DEBUG_UART_BAUDRATE);
    printf("UART RX Buffer: %lu/%lu bytes\n",
           (unsigned long)rv_ringbuf_available(&g_uart_app.shell_buf),
           (unsigned long)RX_BUF_CAPACITY);
    printf("SNN Data Ready: %s\n", g_uart_app.rx_data_ready ? "YES" : "NO");
    printf("SNN Data Count: %lu\n", (unsigned long)g_uart_app.rx_frame_count);
    printf("SNN Response Pending: %s\n",
           g_uart_app.response_pending ? "YES" : "NO");
    for (frame_kind_t kind = FRAME_CONFIG; kind < FRAME_KIND_COUNT; ++kind) {
        frame_view_t view;
        const frame_status_t status = uart_app_get_frame(kind, &view);
        if (view.word_count != 0U) {
            printf("%s frame: %s (%lu words)\n", uart_app_frame_name(kind),
                   uart_app_frame_status_name(status),
                   (unsigned long)view.word_count);
        } else {
            printf("%s frame: %s\n", uart_app_frame_name(kind),
                   uart_app_frame_status_name(status));
        }
    }
    printf("===========================\n");
}

static void set_log_level(const char *name)
{
#if RV_DEBUG_ENABLE_LOGGING
    static const char *const names[] = {"off", "error", "warn", "info",
                                        "debug"};
    static const rv_debug_level_t levels[] = {RV_DEBUG_OFF, RV_DEBUG_ERROR,
                                              RV_DEBUG_WARN, RV_DEBUG_INFO,
                                              RV_DEBUG_DEBUG};
    for (size_t i = 0U; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        if (strcmp(name, names[i]) == 0) {
            rv_debug_set_level(levels[i]);
            printf("Debug log level: %s\n", name);
            return;
        }
    }
    printf("ERROR: log level must be off, error, warn, info, or debug.\n");
#else
    (void)name;
    printf("ERROR: debug logging was disabled at build time.\n");
#endif
}

static void send_user_frame(int argc, char *argv[])
{
    uint32_t high;
    uint32_t low;
    uint32_t expected;
    user_frame_kind_t kind;
    if (argc != 2) {
        printf("ERROR: user requires exactly one 64-bit value.\n");
        print_user_help();
        return;
    }
    if (strcmp(argv[1], "help") == 0) {
        print_user_help();
        return;
    }
    if (uart_app_parse_u64(argv[1], &high, &low) != 0) {
        printf("ERROR: invalid 64-bit value; nothing was sent.\n");
        return;
    }
    kind = uart_app_classify_frame(high, low, &expected);
    printf("Sending user frame: 0x%08lX%08lX (%s)\n", (unsigned long)high,
           (unsigned long)low,
           kind == USER_CONFIG ? "config"
           : kind == USER_TEST ? "test"
           : kind == USER_WORK ? "work"
                               : "control");
    RV_DEBUG_LOGD("uart", "user high=0x%08lX low=0x%08lX expected=%lu",
                  (unsigned long)high, (unsigned long)low,
                  (unsigned long)expected);
    begin_response(kind == USER_WORK ? FRAME_WORK : FRAME_INIT, expected,
                   kind == USER_WORK ? TIMING_COMPUTE : TIMING_RESPONSE);
    noc_lock_state_t tx_state;
    noc_enter_critical(&tx_state);
    noc_fifo_write_frame_words_unlocked(high, low);
    g_uart_app.start_cycles = __get_rv_cycle();
    noc_exit_critical(&tx_state);
}

void uart_app_process_command(const char *cmd)
{
    char copy[UART_APP_CMD_BUF_SIZE];
    char *argv[10];
    int argc = 0;
    bool extra = false;
    char *token;
    if (cmd == NULL || cmd[0] == '\0') {
        return;
    }
    strncpy(copy, cmd, sizeof(copy) - 1U);
    copy[sizeof(copy) - 1U] = '\0';
    token = strtok(copy, " \t");
    while (token != NULL) {
        if (argc < (int)(sizeof(argv) / sizeof(argv[0]))) {
            argv[argc++] = token;
        } else {
            extra = true;
        }
        token = strtok(NULL, " \t");
    }
    if (argc == 0) {
        return;
    }
    output_critical_enter();
    if (strcmp(argv[0], "help") == 0 && argc == 1) {
        print_help();
    } else if (strcmp(argv[0], "status") == 0 && argc == 1) {
        print_status();
    } else if (strcmp(argv[0], "clear") == 0 && argc == 1) {
        rv_ringbuf_reset(&g_uart_app.shell_buf);
        g_shell_command_length = 0;
        g_uart_app.rx_frame_count = 0U;
        g_uart_app.rx_data_ready = 0U;
        g_uart_app.response_pending = 0U;
        g_uart_app.expected_rx_frames = 0U;
        g_uart_app.timing_kind = TIMING_NONE;
        g_uart_app.start_cycles = 0U;
        g_uart_app.end_cycles = 0U;
        printf(
            "UART shell and receive state cleared; hardware was not reset.\n");
    } else if (strcmp(argv[0], "log") == 0 && argc == 2) {
        set_log_level(argv[1]);
    } else if (strcmp(argv[0], "snn") == 0 && argc == 2) {
        if (strcmp(argv[1], "config") == 0) {
            send_frame(FRAME_CONFIG);
        } else if (strcmp(argv[1], "init") == 0) {
            send_frame(FRAME_INIT);
        } else if (strcmp(argv[1], "work") == 0 ||
                   strcmp(argv[1], "input") == 0) {
            send_frame(FRAME_WORK);
        } else if (strcmp(argv[1], "sync") == 0) {
            send_frame(FRAME_SYNC);
        } else if (strcmp(argv[1], "test") == 0) {
            send_frame(FRAME_TEST);
        } else if (strcmp(argv[1], "status") == 0) {
            print_status();
        } else {
            printf("Unknown SNN command: '%s'\n", argv[1]);
        }
    } else if (strcmp(argv[0], "user") == 0 && !extra) {
        send_user_frame(argc, argv);
    } else {
        printf("ERROR: invalid command or argument count: '%s'\n", cmd);
    }
    output_critical_exit();
}

int uart_app_init(void)
{
    int32_t result;
    memset(&g_uart_app, 0, sizeof(g_uart_app));
    result = rv_ringbuf_init(&g_uart_app.shell_buf, g_uart_app.shell_storage,
                             sizeof(g_uart_app.shell_storage));
    if (result != RV_RINGBUF_OK) {
        printf("ERROR: Failed to initialize UART RX ring buffer.\n");
        return -1;
    }
    uart_set_rx_watermark(UART0, FIFO_WATERMARK);
    uart_enable_rxint(UART0);
    result = ECLIC_Register_IRQ(UART0_IRQn, ECLIC_NON_VECTOR_INTERRUPT,
                                ECLIC_LEVEL_TRIGGER, 2, 0, UART0_IRQHandler);
    if (result != 0) {
        printf("ERROR: Failed to register UART0 interrupt (code: %ld)\n",
               (long)result);
        return -1;
    }
    result = ECLIC_Register_IRQ(PAICORE_NOC_IRQn, ECLIC_NON_VECTOR_INTERRUPT,
                                ECLIC_LEVEL_TRIGGER, NOC_IRQ_LEVEL,
                                NOC_IRQ_PRIORITY, paicore_noc_handler);
    if (result != 0) {
        printf("ERROR: Failed to register NoC interrupt (code: %ld)\n",
               (long)result);
        return -1;
    }
#if RV_DEBUG_ENABLE_LOGGING
    rv_debug_set_level(RV_DEBUG_INFO);
#endif
    __enable_irq();
    return 0;
}

int uart_app_service_received_data(void)
{
    output_critical_enter();
    release_timed_out_response();
    if (!g_uart_app.rx_data_ready) {
        output_critical_exit();
        return 0;
    }
    printf("\n[SNN] Read %lu 64-bit data words\n",
           (unsigned long)g_uart_app.rx_frame_count);
    if (g_uart_app.timing_kind == TIMING_COMPUTE) {
        print_cycles("Thread Compute Time: ", g_uart_app.start_cycles,
                     g_uart_app.end_cycles);
    } else if (g_uart_app.timing_kind == TIMING_RESPONSE) {
        print_cycles("Response Time: ", g_uart_app.start_cycles,
                     g_uart_app.end_cycles);
    }
    RV_DEBUG_LOGI("uart", "received %lu frames",
                  (unsigned long)g_uart_app.rx_frame_count);
    g_uart_app.rx_data_ready = 0U;
    g_uart_app.timing_kind = TIMING_NONE;
    output_critical_exit();
    return 1;
}

void uart_app_print_prompt(void)
{
    output_critical_enter();
    if (!g_prompt_printed) {
        printf("UART app ready. Type 'help'.\n");
        g_prompt_printed = 1U;
    }
    printf("> ");
    output_critical_exit();
}
