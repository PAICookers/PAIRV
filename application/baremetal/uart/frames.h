#ifndef UART_FRAMES_H
#define UART_FRAMES_H

#include <stdint.h>

#define SYNC_FRAME_WORDS 2U
#define CONFIG_FRAME_WORDS       1352708U

extern const uint32_t sync_frame[SYNC_FRAME_WORDS];
extern const uint32_t config_frame[CONFIG_FRAME_WORDS];

#endif /* UART_FRAMES_H */
