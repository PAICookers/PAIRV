#ifndef UART_FRAME_H
#define UART_FRAME_H

#include <stddef.h>
#include <stdint.h>

typedef enum frame_kind {
    FRAME_CONFIG = 0U,
    FRAME_INIT,
    FRAME_WORK,
    FRAME_SYNC,
    FRAME_TEST,
    FRAME_KIND_COUNT,
} frame_kind_t;

typedef enum frame_status {
    FRAME_AVAILABLE = 0U,
    FRAME_UNAVAILABLE,
    FRAME_INVALID,
} frame_status_t;

typedef struct uart_frame_view {
    const uint32_t *words;
    size_t word_count;
} frame_view_t;

typedef enum user_frame_kind {
    USER_CONFIG = 0U,
    USER_TEST,
    USER_WORK,
    USER_CONTROL,
} user_frame_kind_t;

frame_status_t uart_app_get_frame(frame_kind_t kind, frame_view_t *view);

#endif /* UART_FRAME_H */
