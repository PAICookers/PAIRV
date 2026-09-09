#ifndef SNN_HEAD_UART_H
#define SNN_HEAD_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "snn_head.h"
#include "snn_head_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SNN_HEAD_UART_HEADER_SIZE 48U
#define SNN_HEAD_UART_VERSION 1U
#define SNN_HEAD_UART_FLOAT32_BYTES 4U
#define SNN_HEAD_UART_BATCH_SIZE 1U
#define SNN_HEAD_UART_INPUT_PAYLOAD_BYTES                                      \
    (SNN_HEAD_UART_BATCH_SIZE * SNN_HEAD_INPUT_FLOAT_COUNT *                   \
     SNN_HEAD_UART_FLOAT32_BYTES)
#define SNN_HEAD_UART_RESULT_PAYLOAD_BYTES                                     \
    (SNN_HEAD_UART_BATCH_SIZE * SNN_HEAD_ACTION_FLOAT_COUNT *                  \
     SNN_HEAD_UART_FLOAT32_BYTES)

typedef enum {
    SNN_HEAD_UART_TYPE_INPUT = 1,
    SNN_HEAD_UART_TYPE_INPUT_ACK = 2,
    SNN_HEAD_UART_TYPE_RESULT = 3,
    SNN_HEAD_UART_TYPE_ERROR = 4,
} snn_head_uart_frame_type_t;

typedef enum {
    SNN_HEAD_UART_STATUS_OK = 0,
    SNN_HEAD_UART_STATUS_BAD_HEADER = 1,
    SNN_HEAD_UART_STATUS_BAD_PAYLOAD = 2,
    SNN_HEAD_UART_STATUS_BAD_SHAPE = 3,
    SNN_HEAD_UART_STATUS_INFERENCE_FAILED = 4,
} snn_head_uart_status_t;

typedef enum {
    SNN_HEAD_UART_DTYPE_NONE = 0,
    SNN_HEAD_UART_DTYPE_FLOAT32_LE = 1,
} snn_head_uart_dtype_t;

typedef enum {
    SNN_HEAD_UART_OK = 0,
    SNN_HEAD_UART_ERR_TIMEOUT = -1,
    SNN_HEAD_UART_ERR_IO = -2,
    SNN_HEAD_UART_ERR_HEADER = -3,
    SNN_HEAD_UART_ERR_PAYLOAD = -4,
    SNN_HEAD_UART_ERR_SHAPE = -5,
    SNN_HEAD_UART_ERR_INFERENCE = -6,
    SNN_HEAD_UART_ERR_ARGUMENT = -7,
} snn_head_uart_result_t;

/*
 * Logical header fields, not a packed wire image. The 48-byte encoding also
 * contains the leading NSNN magic and a header CRC at byte offset 44; crc below
 * is the payload CRC.
 */
typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t header_size;
    uint32_t seq;
    uint32_t length;
    uint32_t crc;
    uint32_t status;
    uint16_t d0;
    uint16_t d1;
    uint16_t d2;
    uint16_t dtype;
    /** Complete-chain cycles; zero when SNN_HEAD_TIMING is disabled. */
    uint64_t cycles;
    uint32_t hz;
} snn_head_uart_header_t;

typedef int (*snn_head_uart_read_fn)(void *ctx, uint8_t *data, size_t length,
                                     uint64_t deadline);
typedef int (*snn_head_uart_write_fn)(void *ctx, const uint8_t *data,
                                      size_t length, uint64_t deadline);
typedef uint64_t (*snn_head_uart_now_fn)(void *ctx);
typedef uint32_t (*snn_head_uart_hz_fn)(void *ctx);
typedef bool (*snn_head_uart_infer_fn)(void *ctx, const float *input,
                                       float *action, uint64_t *cycles);

/*
 * read/write must transfer all length bytes or return an error. deadline is an
 * absolute tick in the same clock domain as now(); hz() reports that clock's
 * frequency. ctx is shared by all callbacks so host tests can keep isolated
 * transport and virtual-clock state.
 */
typedef struct {
    snn_head_uart_read_fn read;
    snn_head_uart_write_fn write;
    snn_head_uart_now_fn now;
    snn_head_uart_hz_fn hz;
    void *ctx;
} snn_head_uart_io_t;

uint32_t snn_head_uart_crc32(const uint8_t *data, size_t length);
void snn_head_uart_header_encode(uint8_t raw[SNN_HEAD_UART_HEADER_SIZE],
                                 const snn_head_uart_header_t *header);
int snn_head_uart_header_decode(const uint8_t raw[SNN_HEAD_UART_HEADER_SIZE],
                                snn_head_uart_header_t *header);
void snn_head_uart_encode_float32_le(uint8_t *wire, const float *values,
                                     size_t count);
void snn_head_uart_decode_float32_le(const uint8_t *wire, float *values,
                                     size_t count);

/**
 * @brief Return optional UART timing for the most recent transaction.
 *
 * Release builds return all-zero values. `result_encode_tx_cycles` includes
 * float encoding, RESULT header creation, and header/payload transmission.
 */
const snn_head_transport_timing_t *snn_head_uart_timing_get(void);

/**
 * @brief Receive one fixed-shape input, run one full inference, and reply.
 *
 * INPUT is float32[1][8][768]; RESULT is float32[1][8][7]. Non-finite input is
 * rejected before ACK/inference, and non-finite output is reported as an
 * inference error. The caller owns input/action storage and result_wire, while
 * infer owns only the interval measured as inference cycles.
 */
int snn_head_uart_process_one(const snn_head_uart_io_t *io, float *input,
                              float *action, uint8_t *result_wire,
                              snn_head_uart_infer_fn infer, void *infer_ctx,
                              uint32_t io_timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SNN_HEAD_UART_H */
