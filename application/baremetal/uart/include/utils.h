#ifndef UART_UTILS_H
#define UART_UTILS_H

#include "uart_frame.h"

int uart_app_parse_u64(const char *text, uint32_t *high, uint32_t *low);
user_frame_kind_t uart_app_classify_frame(uint32_t high, uint32_t low,
                                          uint32_t *expected_frames);
const char *uart_app_frame_name(frame_kind_t kind);
const char *uart_app_frame_status_name(frame_status_t status);

#endif /* UART_UTILS_H */
