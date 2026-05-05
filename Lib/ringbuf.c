#include "ringbuf.h"
#include <stddef.h>

static bool rv_ringbuf_is_valid(const rv_ringbuf_t *rb)
{
    return rb != NULL && rb->storage != NULL && rb->size >= 2U;
}

static uint32_t rv_ringbuf_next_index(const rv_ringbuf_t *rb, uint32_t index)
{
    index++;
    if (index >= rb->size) {
        index = 0U;
    }
    return index;
}

int32_t rv_ringbuf_init(rv_ringbuf_t *rb, uint8_t *storage,
                        uint32_t storage_size)
{
    if (rb == NULL || storage == NULL || storage_size < 2U) {
        return RV_RINGBUF_ERR_INVALID;
    }

    rb->storage = storage;
    rb->size = storage_size;
    rb->head = 0U;
    rb->tail = 0U;
    return RV_RINGBUF_OK;
}

void rv_ringbuf_reset(rv_ringbuf_t *rb)
{
    if (rv_ringbuf_is_valid(rb)) {
        rb->tail = rb->head;
    }
}

int32_t rv_ringbuf_put(rv_ringbuf_t *rb, uint8_t byte)
{
    if (!rv_ringbuf_is_valid(rb)) {
        return RV_RINGBUF_ERR_INVALID;
    }

    uint32_t head = rb->head;
    uint32_t next_head = rv_ringbuf_next_index(rb, head);
    if (next_head == rb->tail) {
        return RV_RINGBUF_ERR_FULL;
    }

    rb->storage[head] = byte;
    rb->head = next_head;
    return RV_RINGBUF_OK;
}

int32_t rv_ringbuf_get(rv_ringbuf_t *rb, uint8_t *byte)
{
    if (!rv_ringbuf_is_valid(rb) || byte == NULL) {
        return RV_RINGBUF_ERR_INVALID;
    }

    uint32_t tail = rb->tail;
    if (rb->head == tail) {
        return RV_RINGBUF_ERR_EMPTY;
    }

    *byte = rb->storage[tail];
    rb->tail = rv_ringbuf_next_index(rb, tail);
    return RV_RINGBUF_OK;
}

void rv_ringbuf_put_byte_cb(uint8_t byte, void *ctx)
{
    rv_ringbuf_t *rb = (rv_ringbuf_t *)ctx;
    if (rb == NULL) {
        return;
    }

    (void)rv_ringbuf_put(rb, byte);
}

uint32_t rv_ringbuf_available(const rv_ringbuf_t *rb)
{
    if (!rv_ringbuf_is_valid(rb)) {
        return 0U;
    }

    uint32_t head = rb->head;
    uint32_t tail = rb->tail;
    if (head >= tail) {
        return head - tail;
    }
    return rb->size - tail + head;
}

uint32_t rv_ringbuf_free_space(const rv_ringbuf_t *rb)
{
    if (!rv_ringbuf_is_valid(rb)) {
        return 0U;
    }

    return (rb->size - 1U) - rv_ringbuf_available(rb);
}

bool rv_ringbuf_is_empty(const rv_ringbuf_t *rb)
{
    if (!rv_ringbuf_is_valid(rb)) {
        return true;
    }

    return rb->head == rb->tail;
}

bool rv_ringbuf_is_full(const rv_ringbuf_t *rb)
{
    if (!rv_ringbuf_is_valid(rb)) {
        return false;
    }

    return rv_ringbuf_next_index(rb, rb->head) == rb->tail;
}
