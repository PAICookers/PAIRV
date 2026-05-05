#include "uart_app.h"

int main(void)
{
    char cmd_buf[UART_APP_CMD_BUF_SIZE];

    /* Bring up the UART/NoC side effects first. The app loop only runs if the
     * IRQ-backed shell and PAICORE receive path are ready. */
    if (uart_app_init() != 0) {
        return -1;
    }

    uart_app_print_prompt();

    while (1) {
        /* 1. Poll the shell without blocking so UART RX IRQs can accumulate
         * bytes independently in the background. */
        int cmd_len = uart_app_poll_command(cmd_buf, sizeof(cmd_buf));

        if (cmd_len > 0) {
            uart_app_process_command(cmd_buf);
            uart_app_print_prompt();
        }

        /* 2. Handle any completed PAICORE receive sequence reported by the
         * shared NoC callback path. */
        if (uart_app_service_received_data() != 0) {
            uart_app_print_prompt();
        }
    }

    return 0;
}
