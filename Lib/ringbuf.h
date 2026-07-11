#ifndef RV_RINGBUF_H
#define RV_RINGBUF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rv_ringbuf_status_e {
    RV_RINGBUF_OK = 0,
    RV_RINGBUF_ERR_INVALID = -1,
    RV_RINGBUF_ERR_FULL = -2,
    RV_RINGBUF_ERR_EMPTY = -3,
} rv_ringbuf_status_t;

typedef struct rv_ringbuf {
    uint8_t *storage;
    uint32_t size;
    volatile uint32_t head;
    volatile uint32_t tail;
} rv_ringbuf_t;

/* Initialize a caller-owned byte ring buffer.
 * One byte is reserved to distinguish full from empty, so usable capacity is
 * storage_size - 1. */
rv_ringbuf_status_t rv_ringbuf_init(rv_ringbuf_t *rb, uint8_t *storage,
                                    uint32_t storage_size);

/* Reset indices only. The caller owns any required IRQ/task synchronization. */
void rv_ringbuf_reset(rv_ringbuf_t *rb);

/* SPSC byte operations: one writer and one reader may run in different
 * contexts, such as UART RX IRQ and a bare-metal main loop. */
rv_ringbuf_status_t rv_ringbuf_put(rv_ringbuf_t *rb, uint8_t byte);
rv_ringbuf_status_t rv_ringbuf_get(rv_ringbuf_t *rb, uint8_t *byte);

uint32_t rv_ringbuf_available(const rv_ringbuf_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* RV_RINGBUF_H */
