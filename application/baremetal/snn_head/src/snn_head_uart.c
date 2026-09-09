/*
 * Platform-independent NSNN framing for the SNN Head application boundary.
 * Callbacks own UART and clock access; this module owns wire encoding,
 * validation, transaction order, and conversion between bytes and float32.
 */
#include "snn_head_uart.h"

#include <math.h>
#include <string.h>

#define SNN_HEAD_UART_HEADER_CRC_OFFSET 44U
#define SNN_HEAD_UART_CRC_POLY UINT32_C(0xedb88320)

static const uint8_t snn_head_uart_magic[] = {'N', 'S', 'N', 'N'};

#if SNN_HEAD_TIMING
static snn_head_transport_timing_t snn_head_uart_timing;
#else
static const snn_head_transport_timing_t snn_head_uart_timing;
#endif

const snn_head_transport_timing_t *snn_head_uart_timing_get(void)
{
    return &snn_head_uart_timing;
}

static uint16_t load16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t load32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint64_t load64(const uint8_t *data)
{
    return (uint64_t)load32(data) | ((uint64_t)load32(data + 4U) << 32U);
}

static void store16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void store32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void store64(uint8_t *data, uint64_t value)
{
    store32(data, (uint32_t)value);
    store32(data + 4U, (uint32_t)(value >> 32U));
}

uint32_t snn_head_uart_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;

    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ SNN_HEAD_UART_CRC_POLY
                                   : crc >> 1U;
        }
    }
    return crc ^ UINT32_MAX;
}

void snn_head_uart_header_encode(uint8_t raw[SNN_HEAD_UART_HEADER_SIZE],
                                 const snn_head_uart_header_t *header)
{
    memcpy(raw, snn_head_uart_magic, sizeof(snn_head_uart_magic));
    raw[4] = header->version;
    raw[5] = header->type;
    store16(raw + 6U, header->header_size);
    store32(raw + 8U, header->seq);
    store32(raw + 12U, header->length);
    store32(raw + 16U, header->crc);
    store32(raw + 20U, header->status);
    store16(raw + 24U, header->d0);
    store16(raw + 26U, header->d1);
    store16(raw + 28U, header->d2);
    store16(raw + 30U, header->dtype);
    store64(raw + 32U, header->cycles);
    store32(raw + 40U, header->hz);
    store32(raw + SNN_HEAD_UART_HEADER_CRC_OFFSET,
            snn_head_uart_crc32(raw, SNN_HEAD_UART_HEADER_CRC_OFFSET));
}

int snn_head_uart_header_decode(const uint8_t raw[SNN_HEAD_UART_HEADER_SIZE],
                                snn_head_uart_header_t *header)
{
    if ((raw == NULL) || (header == NULL)) {
        return SNN_HEAD_UART_ERR_ARGUMENT;
    }

    header->version = raw[4];
    header->type = raw[5];
    header->header_size = load16(raw + 6U);
    header->seq = load32(raw + 8U);
    header->length = load32(raw + 12U);
    header->crc = load32(raw + 16U);
    header->status = load32(raw + 20U);
    header->d0 = load16(raw + 24U);
    header->d1 = load16(raw + 26U);
    header->d2 = load16(raw + 28U);
    header->dtype = load16(raw + 30U);
    header->cycles = load64(raw + 32U);
    header->hz = load32(raw + 40U);

    if ((memcmp(raw, snn_head_uart_magic, sizeof(snn_head_uart_magic)) != 0) ||
        (header->version != SNN_HEAD_UART_VERSION) ||
        (header->header_size != SNN_HEAD_UART_HEADER_SIZE) ||
        (load32(raw + SNN_HEAD_UART_HEADER_CRC_OFFSET) !=
         snn_head_uart_crc32(raw, SNN_HEAD_UART_HEADER_CRC_OFFSET))) {
        return SNN_HEAD_UART_ERR_HEADER;
    }
    return SNN_HEAD_UART_OK;
}

void snn_head_uart_encode_float32_le(uint8_t *wire, const float *values,
                                     size_t count)
{
    for (size_t index = 0U; index < count; ++index) {
        uint32_t bits;
        memcpy(&bits, &values[index], sizeof(bits));
        store32(wire + index * SNN_HEAD_UART_FLOAT32_BYTES, bits);
    }
}

void snn_head_uart_decode_float32_le(const uint8_t *wire, float *values,
                                     size_t count)
{
    for (size_t index = 0U; index < count; ++index) {
        const uint32_t bits =
            load32(wire + index * SNN_HEAD_UART_FLOAT32_BYTES);
        memcpy(&values[index], &bits, sizeof(bits));
    }
}

static uint64_t deadline_after_ms(const snn_head_uart_io_t *io,
                                  uint32_t timeout_ms)
{
    const uint64_t ticks =
        (((uint64_t)timeout_ms * io->hz(io->ctx)) + 999U) / 1000U;
    return io->now(io->ctx) + (ticks != 0U ? ticks : 1U);
}

static int send_header(const snn_head_uart_io_t *io,
                       const snn_head_uart_header_t *header, uint64_t deadline)
{
    uint8_t raw[SNN_HEAD_UART_HEADER_SIZE];

    snn_head_uart_header_encode(raw, header);
    return io->write(io->ctx, raw, sizeof(raw), deadline);
}

static int read_header(const snn_head_uart_io_t *io,
                       uint8_t raw[SNN_HEAD_UART_HEADER_SIZE],
                       uint64_t deadline)
{
    /* KMP fallback preserves an overlapping prefix, e.g. NS + NSNN. */
    static const uint8_t fallback[] = {0U, 0U, 1U, 1U};
    size_t matched = 0U;

    while (matched < sizeof(snn_head_uart_magic)) {
        uint8_t byte;
        const int result = io->read(io->ctx, &byte, 1U, deadline);
        if (result != SNN_HEAD_UART_OK) {
            return result;
        }
        while ((matched > 0U) && (byte != snn_head_uart_magic[matched])) {
            matched = fallback[matched - 1U];
        }
        if (byte == snn_head_uart_magic[matched]) {
            ++matched;
        }
    }
    memcpy(raw, snn_head_uart_magic, sizeof(snn_head_uart_magic));
    return io->read(io->ctx, raw + sizeof(snn_head_uart_magic),
                    SNN_HEAD_UART_HEADER_SIZE - sizeof(snn_head_uart_magic),
                    deadline);
}

static int reply_error(const snn_head_uart_io_t *io, uint32_t seq,
                       uint32_t status, int cause, uint64_t deadline)
{
    const snn_head_uart_header_t header = {
        .version = SNN_HEAD_UART_VERSION,
        .type = SNN_HEAD_UART_TYPE_ERROR,
        .header_size = SNN_HEAD_UART_HEADER_SIZE,
        .seq = seq,
        .status = status,
        .dtype = SNN_HEAD_UART_DTYPE_NONE,
    };
    const int result = send_header(io, &header, deadline);

    return result == SNN_HEAD_UART_OK ? cause : result;
}

static bool valid_input_shape(const snn_head_uart_header_t *header)
{
    return (header->d0 == SNN_HEAD_UART_BATCH_SIZE) &&
           (header->d1 == SNN_HEAD_TIMESTEPS) &&
           (header->d2 == SNN_HEAD_INPUT_DIM) &&
           (header->dtype == SNN_HEAD_UART_DTYPE_FLOAT32_LE);
}

static bool all_finite(const float *values, size_t count)
{
    for (size_t index = 0U; index < count; ++index) {
        if (!isfinite(values[index])) {
            return false;
        }
    }
    return true;
}

int snn_head_uart_process_one(const snn_head_uart_io_t *io, float *input,
                              float *action, uint8_t *result_wire,
                              snn_head_uart_infer_fn infer, void *infer_ctx,
                              uint32_t io_timeout_ms)
{
    uint8_t raw[SNN_HEAD_UART_HEADER_SIZE];
    snn_head_uart_header_t input_header;

    if ((io == NULL) || (io->read == NULL) || (io->write == NULL) ||
        (io->now == NULL) || (io->hz == NULL) || (input == NULL) ||
        (action == NULL) || (result_wire == NULL) || (infer == NULL) ||
        (io_timeout_ms == 0U) || (io->hz(io->ctx) == 0U)) {
        return SNN_HEAD_UART_ERR_ARGUMENT;
    }

#if SNN_HEAD_TIMING
    snn_head_uart_timing = (snn_head_transport_timing_t){0};
#endif
    const uint64_t deadline = deadline_after_ms(io, io_timeout_ms);
    int result = read_header(io, raw, deadline);
    if (result != SNN_HEAD_UART_OK) {
        return result;
    }

    const uint32_t raw_seq = load32(raw + 8U);
    const uint32_t raw_length = load32(raw + 12U);
    result = snn_head_uart_header_decode(raw, &input_header);
    if ((result != SNN_HEAD_UART_OK) ||
        (input_header.type != SNN_HEAD_UART_TYPE_INPUT)) {
        /* Drain only a bounded declared payload. An untrusted oversized body
         * is left for the next NSNN magic scan to discard and resynchronize. */
        if (raw_length <= SNN_HEAD_UART_INPUT_PAYLOAD_BYTES) {
            result = io->read(io->ctx, (uint8_t *)input, raw_length, deadline);
            if (result != SNN_HEAD_UART_OK) {
                return result;
            }
        }
        return reply_error(io, raw_seq, SNN_HEAD_UART_STATUS_BAD_HEADER,
                           SNN_HEAD_UART_ERR_HEADER, deadline);
    }

    if (input_header.length > SNN_HEAD_UART_INPUT_PAYLOAD_BYTES) {
        return reply_error(io, input_header.seq,
                           SNN_HEAD_UART_STATUS_BAD_PAYLOAD,
                           SNN_HEAD_UART_ERR_PAYLOAD, deadline);
    }
    if (input_header.length != SNN_HEAD_UART_INPUT_PAYLOAD_BYTES) {
        result =
            io->read(io->ctx, (uint8_t *)input, input_header.length, deadline);
        if (result != SNN_HEAD_UART_OK) {
            return result;
        }
        return reply_error(io, input_header.seq,
                           SNN_HEAD_UART_STATUS_BAD_PAYLOAD,
                           SNN_HEAD_UART_ERR_PAYLOAD, deadline);
    }

    /* The payload is received as raw float bytes before in-place decoding. */
#if SNN_HEAD_TIMING
    const uint64_t input_payload_rx_start = io->now(io->ctx);
#endif
    result = io->read(io->ctx, (uint8_t *)input,
                      SNN_HEAD_UART_INPUT_PAYLOAD_BYTES, deadline);
#if SNN_HEAD_TIMING
    snn_head_uart_timing.input_payload_rx_cycles =
        io->now(io->ctx) - input_payload_rx_start;
#endif
    if (result != SNN_HEAD_UART_OK) {
        return result;
    }
    if (!valid_input_shape(&input_header)) {
        return reply_error(io, input_header.seq, SNN_HEAD_UART_STATUS_BAD_SHAPE,
                           SNN_HEAD_UART_ERR_SHAPE, deadline);
    }
    if (snn_head_uart_crc32((const uint8_t *)input,
                            SNN_HEAD_UART_INPUT_PAYLOAD_BYTES) !=
        input_header.crc) {
        return reply_error(io, input_header.seq,
                           SNN_HEAD_UART_STATUS_BAD_PAYLOAD,
                           SNN_HEAD_UART_ERR_PAYLOAD, deadline);
    }

#if SNN_HEAD_TIMING
    const uint64_t input_decode_start = io->now(io->ctx);
#endif
    snn_head_uart_decode_float32_le((const uint8_t *)input, input,
                                    SNN_HEAD_INPUT_FLOAT_COUNT);
#if SNN_HEAD_TIMING
    snn_head_uart_timing.input_decode_cycles =
        io->now(io->ctx) - input_decode_start;
#endif
    if (!all_finite(input, SNN_HEAD_INPUT_FLOAT_COUNT)) {
        return reply_error(io, input_header.seq,
                           SNN_HEAD_UART_STATUS_BAD_PAYLOAD,
                           SNN_HEAD_UART_ERR_PAYLOAD, deadline);
    }

    const snn_head_uart_header_t ack = {
        .version = SNN_HEAD_UART_VERSION,
        .type = SNN_HEAD_UART_TYPE_INPUT_ACK,
        .header_size = SNN_HEAD_UART_HEADER_SIZE,
        .seq = input_header.seq,
        .d0 = SNN_HEAD_UART_BATCH_SIZE,
        .d1 = SNN_HEAD_TIMESTEPS,
        .d2 = SNN_HEAD_INPUT_DIM,
        .dtype = SNN_HEAD_UART_DTYPE_FLOAT32_LE,
    };
    result = send_header(io, &ack, deadline);
    if (result != SNN_HEAD_UART_OK) {
        return result;
    }

    uint64_t cycles = 0U;
    if (!infer(infer_ctx, input, action, &cycles)) {
        const uint64_t response_deadline = deadline_after_ms(io, io_timeout_ms);
        return reply_error(io, input_header.seq,
                           SNN_HEAD_UART_STATUS_INFERENCE_FAILED,
                           SNN_HEAD_UART_ERR_INFERENCE, response_deadline);
    }
    if (!all_finite(action, SNN_HEAD_ACTION_FLOAT_COUNT)) {
        const uint64_t response_deadline = deadline_after_ms(io, io_timeout_ms);
        return reply_error(io, input_header.seq,
                           SNN_HEAD_UART_STATUS_INFERENCE_FAILED,
                           SNN_HEAD_UART_ERR_INFERENCE, response_deadline);
    }

#if SNN_HEAD_TIMING
    const uint64_t result_encode_tx_start = io->now(io->ctx);
#endif
    snn_head_uart_encode_float32_le(result_wire, action,
                                    SNN_HEAD_ACTION_FLOAT_COUNT);
    const uint64_t response_deadline = deadline_after_ms(io, io_timeout_ms);
    const snn_head_uart_header_t output = {
        .version = SNN_HEAD_UART_VERSION,
        .type = SNN_HEAD_UART_TYPE_RESULT,
        .header_size = SNN_HEAD_UART_HEADER_SIZE,
        .seq = input_header.seq,
        .length = SNN_HEAD_UART_RESULT_PAYLOAD_BYTES,
        .crc = snn_head_uart_crc32(result_wire,
                                   SNN_HEAD_UART_RESULT_PAYLOAD_BYTES),
        .d0 = SNN_HEAD_UART_BATCH_SIZE,
        .d1 = SNN_HEAD_TIMESTEPS,
        .d2 = SNN_HEAD_ACTION_DIM,
        .dtype = SNN_HEAD_UART_DTYPE_FLOAT32_LE,
        .cycles = cycles,
        .hz = io->hz(io->ctx),
    };
    result = send_header(io, &output, response_deadline);
    if (result != SNN_HEAD_UART_OK) {
        return result;
    }
    result = io->write(io->ctx, result_wire, SNN_HEAD_UART_RESULT_PAYLOAD_BYTES,
                       response_deadline);
#if SNN_HEAD_TIMING
    snn_head_uart_timing.result_encode_tx_cycles =
        io->now(io->ctx) - result_encode_tx_start;
#endif
    return result;
}
