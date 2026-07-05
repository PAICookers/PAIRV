#include "artifact_reader.h"
#include "debug.h"
#include "frame_codec.h"
#include "managed_packet.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef RVRT_TEST_FIXTURE_DIR
#define RVRT_TEST_FIXTURE_DIR "fixtures"
#endif

#define TEST_WORKSPACE_FRAMES 7U
#define TEST_ARTIFACT_ALIGNMENT 8U
#define TEST_PATH_BYTES 512U
#define TEST_VECTOR_BYTES 64U
#define TEST_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define TEST_INPUT_VALUE 0x01U
#define TEST_INPUT_FRAME_GROUP_WIDTH 8U
#define TEST_INPUT_FRAME_HIGH 0x80000800U
#define TEST_INPUT_FRAME_LOW_PREFIX 0x01000001U
#define TEST_INPUT_FRAME_BASE_AXON 0x58U
#define TEST_INPUT_FRAME_GROUP_STRIDE 0x50U
#define TEST_INPUT_FRAME_SLOT_STRIDE 0x08U
#define TEST_OUTPUT_FRAME_HIGH 0x80000000U

typedef struct binary_file_s {
    uint8_t *data;
    size_t size;
} binary_file_t;

typedef struct data_view_s {
    const uint8_t *data;
    size_t size;
} data_view_t;

static const uint8_t TEST_EXPECTED_OUTPUT[TEST_VECTOR_BYTES] = {
    0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x00U,
    0x01U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x7fU,
    0x01U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x7fU,
    0x01U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x7fU,
    0x01U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x7fU,
    0x01U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x7fU,
    0x01U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x7fU,
    0x00U, 0x7fU, 0x7fU, 0x7fU, 0x7fU, 0x7fU, 0x7fU, 0x7fU,
};

static bool frames_equal(const rvrt_frame_t *lhs, const rvrt_frame_t *rhs)
{
    return (lhs->high == rhs->high) && (lhs->low == rhs->low);
}

static rvrt_frame_t expected_input_frame(size_t index)
{
    const uint32_t group = (uint32_t)(index / TEST_INPUT_FRAME_GROUP_WIDTH);
    const uint32_t slot = (uint32_t)(index % TEST_INPUT_FRAME_GROUP_WIDTH);
    const uint32_t axon =
        TEST_INPUT_FRAME_BASE_AXON + (group * TEST_INPUT_FRAME_GROUP_STRIDE) +
        (slot * TEST_INPUT_FRAME_SLOT_STRIDE);
    rvrt_frame_t frame = {TEST_INPUT_FRAME_HIGH,
                          TEST_INPUT_FRAME_LOW_PREFIX | (axon << 8U)};
    return frame;
}

static rvrt_frame_t expected_output_frame(size_t index, uint8_t value)
{
    rvrt_frame_t frame = {TEST_OUTPUT_FRAME_HIGH,
                          ((uint32_t)index << 8U) | (uint32_t)value};
    return frame;
}

static void build_test_input(uint8_t *input, size_t size)
{
    /* Minimal dense input vector: 64 one-byte elements, all enabled. */
    for (size_t i = 0U; i < size; ++i) {
        input[i] = TEST_INPUT_VALUE;
    }
}

static void free_binary(binary_file_t *file)
{
    if (file == NULL) {
        return;
    }
    free(file->data);
    file->data = NULL;
    file->size = 0U;
}

static int read_binary(const char *name, binary_file_t *out)
{
    char path[TEST_PATH_BYTES];
    if ((name == NULL) || (out == NULL)) {
        return 1;
    }

    const int path_len =
        snprintf(path, sizeof(path), "%s/%s", RVRT_TEST_FIXTURE_DIR, name);
    if ((path_len < 0) || ((size_t)path_len >= sizeof(path))) {
        fprintf(stderr, "fixture path too long: %s\n", name);
        return 1;
    }

    FILE *const file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return 1;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        perror("fseek");
        fclose(file);
        return 1;
    }
    const long file_size = ftell(file);
    if (file_size < 0) {
        perror("ftell");
        fclose(file);
        return 1;
    }
    rewind(file);
    if (file_size <= 0) {
        fprintf(stderr, "empty fixture: %s\n", path);
        fclose(file);
        return 1;
    }
    if ((size_t)file_size >
        (SIZE_MAX - (TEST_ARTIFACT_ALIGNMENT - 1U))) {
        fprintf(stderr, "fixture too large: %s\n", path);
        fclose(file);
        return 1;
    }

    const size_t storage_size =
        ((size_t)file_size + (TEST_ARTIFACT_ALIGNMENT - 1U)) &
        ~(size_t)(TEST_ARTIFACT_ALIGNMENT - 1U);
    uint8_t *const data =
        (uint8_t *)aligned_alloc(TEST_ARTIFACT_ALIGNMENT, storage_size);
    if (data == NULL) {
        fprintf(stderr, "out of memory reading: %s\n", path);
        fclose(file);
        return 1;
    }

    const size_t bytes_read = fread(data, 1U, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        free(data);
        fprintf(stderr, "short read: %s\n", path);
        return 1;
    }

    out->data = data;
    out->size = (size_t)file_size;
    return 0;
}

static int expect_status(rvrt_status_t status, const char *stage)
{
    if (status == RVRT_STATUS_OK) {
        return 0;
    }
    fprintf(stderr, "%s failed: %s\n", stage, rvrt_status_string(status));
    return 1;
}

static int expect_artifact_status(rvrt_artifact_status_t status,
                                  const char *stage)
{
    if (status == RVRT_ARTIFACT_OK) {
        return 0;
    }
    fprintf(stderr, "%s failed: %s\n", stage,
            rvrt_artifact_status_string(status));
    return 1;
}

static int expect_size_u32(size_t size, const char *name, uint32_t *out)
{
    if (out == NULL) {
        return 1;
    }
    if (size > UINT32_MAX) {
        fprintf(stderr, "%s too large: %zu bytes\n", name, size);
        return 1;
    }
    *out = (uint32_t)size;
    return 0;
}

static int expect_bytes_equal(const uint8_t *actual, const uint8_t *expected,
                              size_t size, const char *stage)
{
    for (size_t i = 0U; i < size; ++i) {
        if (actual[i] != expected[i]) {
            fprintf(stderr, "%s mismatch offset=%zu got=%02x expected=%02x\n",
                    stage, i, actual[i], expected[i]);
            return 1;
        }
    }
    return 0;
}

static int verify_control_frames(const rvrt_artifact_t *artifact)
{
    rvrt_frame_t init_frame = {0U, 0U};
    rvrt_frame_t sync_frame = {0U, 0U};
    if (expect_status(rvrt_build_init_frame(artifact, 0U, &init_frame),
                      "build init") != 0) {
        return 1;
    }
    if (expect_status(rvrt_build_sync_frame(artifact, 0U, &sync_frame),
                      "build sync") != 0) {
        return 1;
    }
    if ((init_frame.high == 0U) && (init_frame.low == 0U)) {
        fprintf(stderr, "empty init frame\n");
        return 1;
    }
    if ((sync_frame.high == 0U) && (sync_frame.low == 0U)) {
        fprintf(stderr, "empty sync frame\n");
        return 1;
    }
    return 0;
}

static int verify_input_codec(const rvrt_artifact_t *artifact,
                              const data_view_t *input)
{
    uint32_t input_size = 0U;
    if (expect_size_u32(input->size, "input fixture", &input_size) != 0) {
        return 1;
    }

    rvrt_frame_t frames[TEST_WORKSPACE_FRAMES];
    rvrt_input_cursor_t cursor = {0U, 0U, 0U, 0U};
    rvrt_input_cursor_init(&cursor, 0U, 0U, 0U);

    size_t encoded_count = 0U;
    while (true) {
        uint32_t chunk_count = 0U;
        const rvrt_status_t status = rvrt_encode_input_chunk(
            artifact, &cursor, input->data, input_size, frames,
            TEST_WORKSPACE_FRAMES, &chunk_count);
        if ((status != RVRT_STATUS_DONE) &&
            (status != RVRT_STATUS_BUFFER_FULL)) {
            return expect_status(status, "encode input");
        }
        if ((status == RVRT_STATUS_BUFFER_FULL) && (chunk_count == 0U)) {
            fprintf(stderr, "encoder returned full with no frames\n");
            return 1;
        }

        for (uint32_t i = 0U; i < chunk_count; ++i) {
            if (encoded_count >= input->size) {
                fprintf(stderr, "too many input frames\n");
                return 1;
            }
            const rvrt_frame_t expected = expected_input_frame(encoded_count);
            if (!frames_equal(&frames[i], &expected)) {
                fprintf(stderr,
                        "input frame mismatch index=%zu got=%08x%08x "
                        "expected=%08x%08x\n",
                        encoded_count, frames[i].high, frames[i].low,
                        expected.high, expected.low);
                return 1;
            }
            encoded_count++;
        }

        if (status == RVRT_STATUS_DONE) {
            break;
        }
    }

    if (encoded_count != input->size) {
        fprintf(stderr, "input frame count got=%zu expected=%zu\n",
                encoded_count, input->size);
        return 1;
    }

    printf("input frames: %zu\n", encoded_count);
    return 0;
}

static int verify_output_codec(const rvrt_artifact_t *artifact,
                               const data_view_t *expected_output)
{
    uint32_t output_size = 0U;
    if (expect_size_u32(expected_output->size, "expected output fixture",
                        &output_size) != 0) {
        return 1;
    }

    uint8_t *const output = (uint8_t *)calloc(expected_output->size, 1U);
    if (output == NULL) {
        fprintf(stderr, "out of memory decoding output\n");
        return 1;
    }

    uint32_t written_count = 0U;
    for (size_t i = 0U; i < expected_output->size; ++i) {
        const rvrt_frame_t frame =
            expected_output_frame(i, expected_output->data[i]);
        bool written = false;
        const rvrt_status_t status = rvrt_decode_output_frame(
            artifact, 0U, 0U, &frame, output, output_size, &written);
        if (status != RVRT_STATUS_OK) {
            free(output);
            return expect_status(status, "decode output");
        }
        if (written) {
            written_count++;
        }
    }

    const int mismatch = expect_bytes_equal(
        output, expected_output->data, expected_output->size, "output buffer");
    free(output);
    if (mismatch != 0) {
        return 1;
    }

    printf("decoded writes: %u\n", (unsigned)written_count);
    return 0;
}

static int verify_managed_packet(void)
{
    static const uint8_t crc_vector[] = {'1', '2', '3', '4', '5',
                                         '6', '7', '8', '9'};
    const uint32_t vector_crc =
        rvrt_packet_crc32(crc_vector, (uint32_t)sizeof(crc_vector));
    if (vector_crc != 0xCBF43926UL) {
        fprintf(stderr, "crc32 vector mismatch: %08x\n", (unsigned)vector_crc);
        return 1;
    }

    uint8_t payload[8];
    for (size_t i = 0U; i < TEST_ARRAY_SIZE(payload); ++i) {
        payload[i] = (uint8_t)(i + 1U);
    }

    const uint32_t payload_len = (uint32_t)sizeof(payload);
    uint8_t header_bytes[RVRT_PACKET_HEADER_SIZE];
    rvrt_packet_build_header(header_bytes, RVRT_PACKET_COMMAND_RUN_SAMPLE,
                             RVRT_PACKET_STATUS_OK, payload, payload_len);

    rvrt_packet_header_t header = {0};
    if (!rvrt_packet_parse_header(header_bytes, &header)) {
        fprintf(stderr, "packet header parse failed\n");
        return 1;
    }
    if ((header.command != RVRT_PACKET_COMMAND_RUN_SAMPLE) ||
        (header.status != RVRT_PACKET_STATUS_OK) ||
        (header.payload_len != payload_len)) {
        fprintf(stderr, "packet header fields mismatch\n");
        return 1;
    }
    if (!rvrt_packet_validate_crc(&header, payload)) {
        fprintf(stderr, "packet crc validate failed\n");
        return 1;
    }

    payload[0] ^= 0xFFU;
    if (rvrt_packet_validate_crc(&header, payload)) {
        fprintf(stderr, "packet crc accepted corrupted payload\n");
        return 1;
    }

    printf("packet crc32: %08x\n", (unsigned)vector_crc);
    return 0;
}

static int verify_capacity(const rvrt_artifact_t *artifact,
                           const data_view_t *input,
                           const data_view_t *expected_output)
{
    uint32_t input_size = 0U;
    uint32_t output_size = 0U;
    if ((expect_size_u32(input->size, "input fixture", &input_size) != 0) ||
        (expect_size_u32(expected_output->size, "expected output fixture",
                         &output_size) != 0)) {
        return 1;
    }

    rvrt_artifact_capacity_t capacity = {0};
    const rvrt_artifact_status_t status =
        rvrt_artifact_get_capacity(artifact, 0U, 0U, 0U, &capacity);
    if (expect_artifact_status(status, "artifact capacity") != 0) {
        return 1;
    }
    if ((capacity.input_bytes != input_size) ||
        (capacity.output_bytes != output_size)) {
        fprintf(stderr, "capacity bytes mismatch input=%u/%u output=%u/%u\n",
                (unsigned)capacity.input_bytes, (unsigned)input_size,
                (unsigned)capacity.output_bytes, (unsigned)output_size);
        return 1;
    }
    if ((size_t)capacity.rx_frame_count < expected_output->size) {
        fprintf(stderr, "capacity rx frames too small: %u\n",
                (unsigned)capacity.rx_frame_count);
        return 1;
    }
    if (capacity.workspace_frame_count == 0U) {
        fprintf(stderr, "capacity workspace frame count is zero\n");
        return 1;
    }

    printf("capacity: input=%u output=%u rx=%u workspace=%u\n",
           (unsigned)capacity.input_bytes, (unsigned)capacity.output_bytes,
           (unsigned)capacity.rx_frame_count,
           (unsigned)capacity.workspace_frame_count);
    return 0;
}

int main(void)
{
    rv_debug_set_level(RV_DEBUG_OFF);

    binary_file_t artifacts = {0};
    uint8_t input_storage[TEST_VECTOR_BYTES];

    int result = 1;
    if (read_binary("compile_artifacts.bin", &artifacts) != 0) {
        goto cleanup;
    }
    build_test_input(input_storage, TEST_ARRAY_SIZE(input_storage));
    const data_view_t input = {input_storage, TEST_ARRAY_SIZE(input_storage)};
    const data_view_t expected_output = {TEST_EXPECTED_OUTPUT,
                                         TEST_ARRAY_SIZE(TEST_EXPECTED_OUTPUT)};

    rvrt_artifact_t artifact = {0};
    rvrt_artifact_info_t info = {0};
    if (expect_artifact_status(rvrt_artifact_read(artifacts.data,
                                                  artifacts.size,
                                                  &artifact),
                               "artifact read") != 0) {
        goto cleanup;
    }
    if (expect_artifact_status(rvrt_artifact_get_info(&artifact, &info),
                               "artifact info") != 0) {
        goto cleanup;
    }

    printf("artifact bytes: %zu\n", artifacts.size);
    printf("schema version: %u\n", (unsigned)info.schema_version);
    printf("config words: %u\n", (unsigned)info.config_word_count);
    printf("threads: %u\n", (unsigned)info.thread_count);

    if ((verify_managed_packet() != 0) ||
        (verify_capacity(&artifact, &input, &expected_output) != 0) ||
        (verify_control_frames(&artifact) != 0) ||
        (verify_input_codec(&artifact, &input) != 0) ||
        (verify_output_codec(&artifact, &expected_output) != 0)) {
        goto cleanup;
    }

    result = 0;

cleanup:
    free_binary(&artifacts);
    return result;
}
