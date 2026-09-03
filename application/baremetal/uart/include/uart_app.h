#ifndef UART_APP_H
#define UART_APP_H

#define UART_APP_CMD_BUF_SIZE 128

int uart_app_init(void);
int uart_app_poll_command(char *buffer, int buffer_size);
void uart_app_process_command(const char *cmd);
int uart_app_service_received_data(void);
void uart_app_print_prompt(void);

#endif /* UART_APP_H */
