#ifndef _RV_RINGBUF_H
#define _RV_RINGBUF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RV_RINGBUF_OK           0
#define RV_RINGBUF_ERR_INVALID  (-1)
#define RV_RINGBUF_ERR_FULL     (-2)
#define RV_RINGBUF_ERR_EMPTY    (-3)

typedef struct rv_ringbuf {
    uint8_t *storage;
    uint32_t size;
    volatile uint32_t head;
    volatile uint32_t tail;
} rv_ringbuf_t;

/* Initialize a caller-owned byte ring buffer.
 * One byte is reserved to distinguish full from empty, so usable capacity is
 * storage_size - 1. */
int32_t rv_ringbuf_init(rv_ringbuf_t *rb, uint8_t *storage,
                        uint32_t storage_size);

/* Reset indices only. The caller owns any required IRQ/task synchronization. */
void rv_ringbuf_reset(rv_ringbuf_t *rb);

/* SPSC byte operations: one writer and one reader may run in different
 * contexts, such as UART RX IRQ and a bare-metal main loop. */
int32_t rv_ringbuf_put(rv_ringbuf_t *rb, uint8_t byte);
int32_t rv_ringbuf_get(rv_ringbuf_t *rb, uint8_t *byte);

/* Generic byte-sink adapter for callback-style producers such as UART RX FIFO
 * drain helpers. The callback context must point to `rv_ringbuf_t`. */
void rv_ringbuf_put_byte_cb(uint8_t byte, void *ctx);

uint32_t rv_ringbuf_available(const rv_ringbuf_t *rb);
uint32_t rv_ringbuf_free_space(const rv_ringbuf_t *rb);
bool rv_ringbuf_is_empty(const rv_ringbuf_t *rb);
bool rv_ringbuf_is_full(const rv_ringbuf_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* _RV_RINGBUF_H */
