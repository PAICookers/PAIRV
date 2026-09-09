/*
 * In-memory integration test for the production NSNN UART endpoint.
 * It drives the complete chain through mocked NoC and also verifies malformed
 * frames, stream recovery, CRC, shape, and non-finite tensor rejection.
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nuclei_sdk_soc.h"
#include "snn_head.h"
#include "snn_head_uart.h"

int snn_head_host_load_artifacts(const char *asset_dir);

#ifndef SNN_HEAD_ASSET_DIR
#define SNN_HEAD_ASSET_DIR "assets"
#endif

#define HOST_UART_HZ 100000000U
#define HOST_UART_SEQUENCE 20260906U

typedef struct {
    const uint8_t *rx;
    size_t rx_size;
    size_t rx_pos;
    uint8_t
        tx[SNN_HEAD_UART_HEADER_SIZE * 2U + SNN_HEAD_UART_RESULT_PAYLOAD_BYTES];
    size_t tx_size;
    uint64_t ticks;
} host_uart_io_t;

static uint32_t g_infer_calls;

static bool reject_profile_write(void *ctx, const char *data, size_t length)
{
    uint32_t *const calls = ctx;
    (void)data;
    (void)length;
    ++*calls;
    return false;
}

static int host_read(void *ctx, uint8_t *data, size_t length, uint64_t deadline)
{
    host_uart_io_t *const io = ctx;
    if ((io->ticks++ >= deadline) || (length > io->rx_size - io->rx_pos)) {
        return SNN_HEAD_UART_ERR_TIMEOUT;
    }
    memcpy(data, io->rx + io->rx_pos, length);
    io->rx_pos += length;
    return SNN_HEAD_UART_OK;
}

static int host_write(void *ctx, const uint8_t *data, size_t length,
                      uint64_t deadline)
{
    host_uart_io_t *const io = ctx;
    if ((io->ticks++ >= deadline) || (length > sizeof(io->tx) - io->tx_size)) {
        return SNN_HEAD_UART_ERR_TIMEOUT;
    }
    memcpy(io->tx + io->tx_size, data, length);
    io->tx_size += length;
    return SNN_HEAD_UART_OK;
}

static uint64_t host_now(void *ctx)
{
    host_uart_io_t *const io = ctx;
    return ++io->ticks;
}

static uint32_t host_hz(void *ctx)
{
    (void)ctx;
    return HOST_UART_HZ;
}

static bool run_complete_chain(void *ctx, const float *input, float *action,
                               uint64_t *cycles)
{
    (void)ctx;
    ++g_infer_calls;
    const uint64_t start = (uint64_t)__get_rv_cycle();
    const bool success = snn_head_run_chunk(input, action);
    *cycles = (uint64_t)__get_rv_cycle() - start;
    return success;
}

static bool return_nonfinite_output(void *ctx, const float *input,
                                    float *action, uint64_t *cycles)
{
    (void)ctx;
    (void)input;
    ++g_infer_calls;
    memset(action, 0, SNN_HEAD_ACTION_FLOAT_COUNT * sizeof(*action));
    action[0] = INFINITY;
    *cycles = 1U;
    return true;
}

static bool return_zero_output(void *ctx, const float *input, float *action,
                               uint64_t *cycles)
{
    (void)ctx;
    (void)input;
    ++g_infer_calls;
    memset(action, 0, SNN_HEAD_ACTION_FLOAT_COUNT * sizeof(*action));
    *cycles = 1U;
    return true;
}

static void fill_random_input(float *input)
{
    uint32_t state = HOST_UART_SEQUENCE;
    for (uint32_t index = 0U; index < SNN_HEAD_INPUT_FLOAT_COUNT; ++index) {
        state = state * 1664525U + 1013904223U;
        input[index] = (float)(state >> 8U) * (2.0f / 16777215.0f) - 1.0f;
    }
}

static size_t make_input_frame(uint8_t *frame, float *input)
{
    fill_random_input(input);
    uint8_t *const payload = frame + SNN_HEAD_UART_HEADER_SIZE;
    snn_head_uart_encode_float32_le(payload, input, SNN_HEAD_INPUT_FLOAT_COUNT);
    const snn_head_uart_header_t header = {
        .version = SNN_HEAD_UART_VERSION,
        .type = SNN_HEAD_UART_TYPE_INPUT,
        .header_size = SNN_HEAD_UART_HEADER_SIZE,
        .seq = HOST_UART_SEQUENCE,
        .length = SNN_HEAD_UART_INPUT_PAYLOAD_BYTES,
        .crc = snn_head_uart_crc32(payload, SNN_HEAD_UART_INPUT_PAYLOAD_BYTES),
        .d0 = SNN_HEAD_UART_BATCH_SIZE,
        .d1 = SNN_HEAD_TIMESTEPS,
        .d2 = SNN_HEAD_INPUT_DIM,
        .dtype = SNN_HEAD_UART_DTYPE_FLOAT32_LE,
    };
    snn_head_uart_header_encode(frame, &header);
    return SNN_HEAD_UART_HEADER_SIZE + SNN_HEAD_UART_INPUT_PAYLOAD_BYTES;
}

#define CHECK(condition, message)                                              \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed: %s\n", message);                    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void)
{
    uint8_t
        request[SNN_HEAD_UART_HEADER_SIZE + SNN_HEAD_UART_INPUT_PAYLOAD_BYTES];
    static uint8_t recovery_stream[2U * sizeof(request)];
    uint8_t result_wire[SNN_HEAD_UART_RESULT_PAYLOAD_BYTES];
    float input[SNN_HEAD_INPUT_FLOAT_COUNT];
    float action[SNN_HEAD_ACTION_FLOAT_COUNT];

    /* Standard CRC-32/ISO-HDLC check vector locks down protocol parameters. */
    static const uint8_t crc_check[] = "123456789";
    CHECK(snn_head_uart_crc32(crc_check, sizeof(crc_check) - 1U) ==
              UINT32_C(0xcbf43926),
          "CRC-32/ISO-HDLC check vector");
    CHECK(snn_head_host_load_artifacts(SNN_HEAD_ASSET_DIR) == 0,
          "load all five artifacts");
    const size_t request_size = make_input_frame(request, input);
    host_uart_io_t host = {.rx = request, .rx_size = request_size};
    const snn_head_uart_io_t io = {
        .read = host_read,
        .write = host_write,
        .now = host_now,
        .hz = host_hz,
        .ctx = &host,
    };

    g_infer_calls = 0U;
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    run_complete_chain, NULL,
                                    1000U) == SNN_HEAD_UART_OK,
          "complete UART inference transaction");
    CHECK(g_infer_calls == 1U, "one complete-chain inference");
    const snn_head_transport_timing_t *uart_timing = snn_head_uart_timing_get();
    const snn_head_profile_t *const model_timing = snn_head_profile_get();
    CHECK(uart_timing->input_payload_rx_cycles > 0U,
          "measure raw input payload receive");
    CHECK(uart_timing->input_decode_cycles > 0U, "measure float decoding");
    CHECK(uart_timing->result_encode_tx_cycles > 0U,
          "measure result encoding and transmit");
    for (uint32_t index = 0U; index < SNN_HEAD_LAYER_COUNT; ++index) {
        CHECK(model_timing->layers[index].total_cycles > 0U,
              "measure each layer total");
        CHECK(model_timing->layers[index].sync_round_trip_cycles > 0U,
              "measure each SYNC round trip");
        CHECK(model_timing->layers[index].total_cycles >=
                  model_timing->layers[index].sync_round_trip_cycles,
              "SYNC round trips are bounded by layer total");
        CHECK(model_timing->layers[index].config_submit_cycles > 0U,
              "measure config transmission");
        CHECK(model_timing->layers[index].input_encode_cycles > 0U,
              "measure input encoding");
        CHECK(model_timing->layers[index].input_submit_cycles > 0U,
              "measure input transmission");
        CHECK(model_timing->layers[index].init_round_trip_cycles > 0U,
              "measure INIT round trip");
    }
    uint32_t profile_write_calls = 0U;
    CHECK(!snn_head_profile_write_report(reject_profile_write,
                                         &profile_write_calls, HOST_UART_HZ, 0U,
                                         uart_timing),
          "propagate timing report write failure");
    CHECK(profile_write_calls == 1U, "stop timing report after write failure");
    CHECK(host.tx_size == SNN_HEAD_UART_HEADER_SIZE * 2U +
                              SNN_HEAD_UART_RESULT_PAYLOAD_BYTES,
          "ACK, RESULT, and action payload size");

    snn_head_uart_header_t ack;
    snn_head_uart_header_t result;
    CHECK(snn_head_uart_header_decode(host.tx, &ack) == SNN_HEAD_UART_OK,
          "decode ACK header");
    CHECK((ack.type == SNN_HEAD_UART_TYPE_INPUT_ACK) &&
              (ack.seq == HOST_UART_SEQUENCE) && (ack.length == 0U) &&
              (ack.status == SNN_HEAD_UART_STATUS_OK) &&
              (ack.d0 == SNN_HEAD_UART_BATCH_SIZE) &&
              (ack.d1 == SNN_HEAD_TIMESTEPS) && (ack.d2 == SNN_HEAD_INPUT_DIM),
          "ACK contract");
    CHECK(snn_head_uart_header_decode(host.tx + SNN_HEAD_UART_HEADER_SIZE,
                                      &result) == SNN_HEAD_UART_OK,
          "decode RESULT header");
    CHECK((result.type == SNN_HEAD_UART_TYPE_RESULT) &&
              (result.seq == HOST_UART_SEQUENCE) &&
              (result.length == SNN_HEAD_UART_RESULT_PAYLOAD_BYTES) &&
              (result.status == SNN_HEAD_UART_STATUS_OK) &&
              (result.d0 == SNN_HEAD_UART_BATCH_SIZE) &&
              (result.d1 == SNN_HEAD_TIMESTEPS) &&
              (result.d2 == SNN_HEAD_ACTION_DIM) &&
              (result.dtype == SNN_HEAD_UART_DTYPE_FLOAT32_LE) &&
              (result.cycles > 0U) && (result.hz == HOST_UART_HZ),
          "RESULT metadata");
    const uint8_t *const payload = host.tx + SNN_HEAD_UART_HEADER_SIZE * 2U;
    CHECK(snn_head_uart_crc32(payload, SNN_HEAD_UART_RESULT_PAYLOAD_BYTES) ==
              result.crc,
          "RESULT payload CRC");
    snn_head_uart_decode_float32_le(payload, action,
                                    SNN_HEAD_ACTION_FLOAT_COUNT);
    for (uint32_t index = 0U; index < SNN_HEAD_ACTION_FLOAT_COUNT; ++index) {
        CHECK(isfinite(action[index]), "finite action value");
    }

    make_input_frame(request, input);
    snn_head_uart_header_t input_header;
    CHECK(snn_head_uart_header_decode(request, &input_header) ==
              SNN_HEAD_UART_OK,
          "decode request for bad header test");
    ++input_header.version;
    snn_head_uart_header_encode(request, &input_header);
    memcpy(recovery_stream, request, request_size);
    make_input_frame(recovery_stream + request_size, input);
    host = (host_uart_io_t){.rx = recovery_stream,
                            .rx_size = sizeof(recovery_stream)};
    g_infer_calls = 0U;
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    return_zero_output, NULL,
                                    1000U) == SNN_HEAD_UART_ERR_HEADER,
          "reject bad header and drain its payload");
    snn_head_uart_header_t error;
    CHECK((host.tx_size == SNN_HEAD_UART_HEADER_SIZE) &&
              (snn_head_uart_header_decode(host.tx, &error) ==
               SNN_HEAD_UART_OK) &&
              (error.type == SNN_HEAD_UART_TYPE_ERROR) &&
              (error.status == SNN_HEAD_UART_STATUS_BAD_HEADER),
          "bad header ERROR response");
    host.tx_size = 0U;
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    return_zero_output, NULL,
                                    1000U) == SNN_HEAD_UART_OK,
          "recover at the following complete frame");
    CHECK(g_infer_calls == 1U, "infer only the recovered frame");

    recovery_stream[0] = 'N';
    recovery_stream[1] = 'S';
    make_input_frame(recovery_stream + 2U, input);
    host =
        (host_uart_io_t){.rx = recovery_stream, .rx_size = request_size + 2U};
    g_infer_calls = 0U;
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    return_zero_output, NULL,
                                    1000U) == SNN_HEAD_UART_OK,
          "resynchronize after overlapping NS prefix");
    CHECK(g_infer_calls == 1U, "infer overlapping-prefix frame once");

    make_input_frame(request, input);
    CHECK(snn_head_uart_header_decode(request, &input_header) ==
              SNN_HEAD_UART_OK,
          "decode request for shape test");
    ++input_header.d2;
    snn_head_uart_header_encode(request, &input_header);
    host = (host_uart_io_t){.rx = request, .rx_size = request_size};
    g_infer_calls = 0U;
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    return_zero_output, NULL,
                                    1000U) == SNN_HEAD_UART_ERR_SHAPE,
          "reject wrong input shape");
    CHECK(g_infer_calls == 0U, "do not infer wrong input shape");

    make_input_frame(request, input);
    CHECK(snn_head_uart_header_decode(request, &input_header) ==
              SNN_HEAD_UART_OK,
          "decode request for short length test");
    input_header.length = SNN_HEAD_UART_FLOAT32_BYTES;
    snn_head_uart_header_encode(request, &input_header);
    host = (host_uart_io_t){.rx = request,
                            .rx_size = SNN_HEAD_UART_HEADER_SIZE +
                                       SNN_HEAD_UART_FLOAT32_BYTES};
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    return_zero_output, NULL,
                                    1000U) == SNN_HEAD_UART_ERR_PAYLOAD,
          "reject short input payload length");

    make_input_frame(request, input);
    CHECK(snn_head_uart_header_decode(request, &input_header) ==
              SNN_HEAD_UART_OK,
          "decode request for oversized length test");
    input_header.length = SNN_HEAD_UART_INPUT_PAYLOAD_BYTES + 1U;
    snn_head_uart_header_encode(request, &input_header);
    memcpy(recovery_stream, request, request_size);
    make_input_frame(recovery_stream + request_size, input);
    host = (host_uart_io_t){.rx = recovery_stream,
                            .rx_size = sizeof(recovery_stream)};
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    return_zero_output, NULL,
                                    1000U) == SNN_HEAD_UART_ERR_PAYLOAD,
          "reject oversized input payload length");
    host.tx_size = 0U;
    g_infer_calls = 0U;
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    return_zero_output, NULL,
                                    1000U) == SNN_HEAD_UART_OK,
          "resynchronize after oversized input frame");
    CHECK(g_infer_calls == 1U, "infer the frame after resynchronization");

    make_input_frame(request, input);
    host = (host_uart_io_t){.rx = request, .rx_size = request_size - 1U};
    g_infer_calls = 0U;
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    return_zero_output, NULL,
                                    1000U) == SNN_HEAD_UART_ERR_TIMEOUT,
          "reject truncated input payload");
    CHECK((g_infer_calls == 0U) && (host.tx_size == 0U),
          "truncated input has no response or inference");

    make_input_frame(request, input);
    request[SNN_HEAD_UART_HEADER_SIZE] ^= 1U;
    host = (host_uart_io_t){.rx = request, .rx_size = request_size};
    g_infer_calls = 0U;
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    run_complete_chain, NULL,
                                    1000U) == SNN_HEAD_UART_ERR_PAYLOAD,
          "reject corrupted payload");
    CHECK(g_infer_calls == 0U, "do not infer corrupt payload");
    uart_timing = snn_head_uart_timing_get();
    CHECK((uart_timing->input_payload_rx_cycles > 0U) &&
              (uart_timing->input_decode_cycles == 0U) &&
              (uart_timing->result_encode_tx_cycles == 0U),
          "failed CRC does not decode or return a result");
    CHECK((host.tx_size == SNN_HEAD_UART_HEADER_SIZE) &&
              (snn_head_uart_header_decode(host.tx, &error) ==
               SNN_HEAD_UART_OK) &&
              (error.type == SNN_HEAD_UART_TYPE_ERROR) &&
              (error.status == SNN_HEAD_UART_STATUS_BAD_PAYLOAD),
          "bad payload ERROR response");

    make_input_frame(request, input);
    const float nan_value = NAN;
    snn_head_uart_encode_float32_le(request + SNN_HEAD_UART_HEADER_SIZE,
                                    &nan_value, 1U);
    CHECK(snn_head_uart_header_decode(request, &input_header) ==
              SNN_HEAD_UART_OK,
          "decode request for non-finite input test");
    input_header.crc = snn_head_uart_crc32(request + SNN_HEAD_UART_HEADER_SIZE,
                                           SNN_HEAD_UART_INPUT_PAYLOAD_BYTES);
    snn_head_uart_header_encode(request, &input_header);
    host = (host_uart_io_t){.rx = request, .rx_size = request_size};
    g_infer_calls = 0U;
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    run_complete_chain, NULL,
                                    1000U) == SNN_HEAD_UART_ERR_PAYLOAD,
          "reject non-finite input");
    CHECK(g_infer_calls == 0U, "do not infer non-finite input");
    CHECK((host.tx_size == SNN_HEAD_UART_HEADER_SIZE) &&
              (snn_head_uart_header_decode(host.tx, &error) ==
               SNN_HEAD_UART_OK) &&
              (error.type == SNN_HEAD_UART_TYPE_ERROR) &&
              (error.status == SNN_HEAD_UART_STATUS_BAD_PAYLOAD),
          "non-finite input ERROR response");

    make_input_frame(request, input);
    host = (host_uart_io_t){.rx = request, .rx_size = request_size};
    g_infer_calls = 0U;
    CHECK(snn_head_uart_process_one(&io, input, action, result_wire,
                                    return_nonfinite_output, NULL,
                                    1000U) == SNN_HEAD_UART_ERR_INFERENCE,
          "reject non-finite output");
    CHECK(g_infer_calls == 1U, "run inference before output validation");
    CHECK((host.tx_size == SNN_HEAD_UART_HEADER_SIZE * 2U) &&
              (snn_head_uart_header_decode(host.tx + SNN_HEAD_UART_HEADER_SIZE,
                                           &error) == SNN_HEAD_UART_OK) &&
              (error.type == SNN_HEAD_UART_TYPE_ERROR) &&
              (error.status == SNN_HEAD_UART_STATUS_INFERENCE_FAILED),
          "non-finite output ERROR response");

    puts("SNN Head UART host integration: PASS");
    return 0;
}
