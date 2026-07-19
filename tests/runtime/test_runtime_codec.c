#include "artifact_reader.h"
#include "data.h"
#include "debug.h"
#include "frame_codec.h"
#include "managed_packet.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RVRT_TEST_ASSET_DIR
#define RVRT_TEST_ASSET_DIR "assets"
#endif

#ifndef RVRT_MNIST_ASSET_DIR
#define RVRT_MNIST_ASSET_DIR "../../application/runtime/mnist/assets"
#endif

#define TEST_ARTIFACT_ALIGNMENT 8U
#define TEST_PATH_BYTES 512U
#define TEST_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define TEST_DATA_ELEMENTS 9U
#define TEST_INPUT_VALUE 0x01U
#define TEST_WORK_DATA_HIGH 0x80000000U
#define TEST_WORK_VOLTAGE_HIGH 0xA0000000U
#define TEST_DTYPE_UINT8 7U
#define TEST_DTYPE_INT32 9U
#define TEST_VOLTAGE_LANES 4U
#define TEST_VOLTAGE_COMPLETE_MASK 0x0FU

_Static_assert(sizeof(rvrt_voltage_decode_state_t) == 5U,
               "voltage decode state must not contain padding");

typedef struct binary_file_s {
    uint8_t *data;
    size_t size;
} binary_file_t;

typedef struct voltage_event_s {
    uint32_t elem_idx;
    uint32_t lane;
} voltage_event_t;

static const uint8_t TEST_EXPECTED_DATA[TEST_DATA_ELEMENTS] = {
    0x01U, 0x7fU, 0x80U, 0x55U, 0xaaU, 0xfeU, 0x10U, 0x20U, 0x30U,
};

static const uint32_t TEST_VOLTAGE_BASES[TEST_DATA_ELEMENTS] = {
    0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 32U,
};

static void free_binary(binary_file_t *file)
{
    if (file == NULL) {
        return;
    }
    free(file->data);
    file->data = NULL;
    file->size = 0U;
}

static int read_binary_at(const char *directory, const char *name,
                          binary_file_t *out)
{
    char path[TEST_PATH_BYTES];
    if ((directory == NULL) || (name == NULL) || (out == NULL)) {
        return 1;
    }

    const int path_len = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if ((path_len < 0) || ((size_t)path_len >= sizeof(path))) {
        fprintf(stderr, "asset path too long: %s\n", name);
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
    if (file_size <= 0L) {
        fprintf(stderr, "invalid asset: %s\n", path);
        fclose(file);
        return 1;
    }
    if ((size_t)file_size > (SIZE_MAX - (TEST_ARTIFACT_ALIGNMENT - 1U))) {
        fprintf(stderr, "asset too large: %s\n", path);
        fclose(file);
        return 1;
    }
    rewind(file);

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

static int read_binary(const char *name, binary_file_t *out)
{
    return read_binary_at(RVRT_TEST_ASSET_DIR, name, out);
}

static int expect_status(rvrt_status_t actual, rvrt_status_t expected,
                         const char *stage)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s status=%s expected=%s\n", stage,
            rvrt_status_string(actual), rvrt_status_string(expected));
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

static rvrt_frame_t work_frame(uint32_t high, uint32_t axon_bit_idx,
                               uint8_t payload)
{
    const rvrt_frame_t frame = {high, (axon_bit_idx << 8U) | (uint32_t)payload};
    return frame;
}

static rvrt_frame_t work_frame_at_timestep(uint32_t high, uint32_t timestep,
                                           uint32_t axon_bit_idx,
                                           uint8_t payload)
{
    rvrt_frame_t frame = work_frame(high, axon_bit_idx, payload);
    frame.high |= ((timestep >> 7U) & 0x1U) << 28U;
    frame.low |= (timestep & 0x7FU) << 17U;
    return frame;
}

static rvrt_frame_t voltage_frame(uint32_t base, uint32_t lane, uint32_t value)
{
    const uint8_t payload = (uint8_t)(value >> (lane * 8U));
    return work_frame(TEST_WORK_VOLTAGE_HIGH, base + lane * 8U, payload);
}

static int read_artifact(const char *fixture, binary_file_t *file,
                         rvrt_artifact_t *artifact)
{
    if (read_binary(fixture, file) != 0) {
        return 1;
    }
    return expect_artifact_status(
        rvrt_artifact_read(file->data, file->size, artifact), fixture);
}

static int verify_artifact_basics(const rvrt_artifact_t *artifact,
                                  uint32_t input_bytes, uint32_t output_bytes,
                                  uint32_t rx_frame_count)
{
    rvrt_artifact_info_t info = {0};
    rvrt_artifact_capacity_t capacity = {0};
    uint32_t cpu_task_count = UINT32_MAX;
    if ((expect_artifact_status(rvrt_artifact_get_info(artifact, &info),
                                "artifact info") != 0) ||
        (expect_artifact_status(
             rvrt_artifact_get_capacity(artifact, 0U, &capacity),
             "artifact capacity") != 0) ||
        (expect_artifact_status(
             rvrt_artifact_cpu_task_count(artifact, &cpu_task_count),
             "cpu task count") != 0)) {
        return 1;
    }
    if ((info.schema_version != 1U) || (info.thread_count != 1U) ||
        (capacity.input_bytes != input_bytes) ||
        (capacity.final_output_bytes != output_bytes) ||
        (capacity.rx_frame_count != rx_frame_count) || (cpu_task_count != 0U)) {
        fprintf(stderr,
                "artifact metadata mismatch schema=%u threads=%u input=%u "
                "output=%u rx=%u cpu_tasks=%u\n",
                (unsigned)info.schema_version, (unsigned)info.thread_count,
                (unsigned)capacity.input_bytes,
                (unsigned)capacity.final_output_bytes,
                (unsigned)capacity.rx_frame_count, (unsigned)cpu_task_count);
        return 1;
    }
    return 0;
}

static int verify_control_frames(const rvrt_artifact_t *artifact)
{
    rvrt_frame_t init_frame = {0U, 0U};
    rvrt_frame_t sync_frame = {0U, 0U};
    rvrt_artifact_runtime_t runtime = {0};
    if ((expect_artifact_status(
             rvrt_artifact_thread_runtime(artifact, 0U, &runtime),
             "thread runtime") != 0) ||
        (expect_status(rvrt_build_init_frame(artifact, 0U, &init_frame),
                       RVRT_STATUS_OK, "build init") != 0) ||
        (expect_status(rvrt_build_sync_frame(artifact, 0U, runtime.sync_steps,
                                             &sync_frame),
                       RVRT_STATUS_OK, "build sync") != 0)) {
        return 1;
    }
    if (((init_frame.high | init_frame.low) == 0U) ||
        ((sync_frame.high | sync_frame.low) == 0U)) {
        fprintf(stderr, "empty control frame\n");
        return 1;
    }
    return 0;
}

static int verify_input_codec(const rvrt_artifact_t *artifact)
{
    rvrt_artifact_input_mapping_view_t view = {0};
    if (expect_artifact_status(
            rvrt_artifact_get_input_mapping_view(artifact, 0U, 0U, &view),
            "input mapping view") != 0) {
        return 1;
    }
    if ((view.entry_count != 1U) || (view.bit_width != 8U)) {
        fprintf(stderr, "input mapping mismatch entries=%u width=%u\n",
                (unsigned)view.entry_count, (unsigned)view.bit_width);
        return 1;
    }

    const uint8_t input[] = {TEST_INPUT_VALUE};
    rvrt_frame_t frame = {0U, 0U};
    rvrt_input_cursor_t cursor = {0U, 0U};
    uint32_t frame_count = 0U;
    rvrt_input_cursor_init(&cursor, 0U);
    rvrt_status_t status = rvrt_encode_input_chunk(
        &view, &cursor, input, sizeof(input), &frame, 1U, &frame_count);
    if ((expect_status(status, RVRT_STATUS_DONE, "encode input") != 0) ||
        (frame_count != 1U) ||
        (((frame.high >> RVRT_FRAME_TYPE_OFFSET) & 0x3U) !=
         RVRT_FRAME_TYPE_WORK) ||
        (((frame.high >> RVRT_FRAME_WORK_KIND_OFFSET) & 0x1U) !=
         RVRT_FRAME_WORK_KIND_DATA) ||
        ((frame.low & 0xFFU) != TEST_INPUT_VALUE)) {
        fprintf(stderr, "input frame mismatch high=%08x low=%08x count=%u\n",
                (unsigned)frame.high, (unsigned)frame.low,
                (unsigned)frame_count);
        return 1;
    }

    const uint8_t zero[] = {0U};
    rvrt_input_cursor_init(&cursor, 0U);
    frame_count = UINT32_MAX;
    status = rvrt_encode_input_chunk(&view, &cursor, zero, sizeof(zero), &frame,
                                     1U, &frame_count);
    if ((expect_status(status, RVRT_STATUS_DONE, "encode zero input") != 0) ||
        (frame_count != 0U)) {
        fprintf(stderr, "zero input emitted frames=%u\n",
                (unsigned)frame_count);
        return 1;
    }
    return 0;
}

static int verify_data_codec(const rvrt_artifact_t *artifact)
{
    rvrt_artifact_output_mapping_view_t view = {0};
    if (expect_artifact_status(
            rvrt_artifact_get_output_mapping_view(artifact, 0U, 0U, &view),
            "data mapping view") != 0) {
        return 1;
    }
    if ((view.kind != RVRT_OUTPUT_DATA) || (view.dtype != TEST_DTYPE_UINT8) ||
        (view.target_lcn != 0U) || (view.entry_count != TEST_DATA_ELEMENTS) ||
        (view.element_count != TEST_DATA_ELEMENTS)) {
        fprintf(stderr,
                "data mapping mismatch kind=%u dtype=%u lcn=%u entries=%u "
                "elements=%u\n",
                (unsigned)view.kind, (unsigned)view.dtype,
                (unsigned)view.target_lcn, (unsigned)view.entry_count,
                (unsigned)view.element_count);
        return 1;
    }

    uint8_t output[TEST_DATA_ELEMENTS] = {0};
    for (uint32_t i = 0U; i < TEST_DATA_ELEMENTS; ++i) {
        const rvrt_frame_t frame =
            work_frame(TEST_WORK_DATA_HIGH, i, TEST_EXPECTED_DATA[i]);
        bool written = false;
        const rvrt_status_t status = rvrt_decode_output_frame(
            &view, &frame, output, TEST_DATA_ELEMENTS, &written);
        if ((expect_status(status, RVRT_STATUS_OK, "decode DATA") != 0) ||
            !written || (output[i] != TEST_EXPECTED_DATA[i])) {
            fprintf(stderr, "DATA decode mismatch index=%u\n", (unsigned)i);
            return 1;
        }
    }

    const rvrt_frame_t unmapped =
        work_frame(TEST_WORK_DATA_HIGH, TEST_DATA_ELEMENTS, 0x42U);
    bool written = true;
    if ((expect_status(rvrt_decode_output_frame(&view, &unmapped, output,
                                                TEST_DATA_ELEMENTS, &written),
                       RVRT_STATUS_OK, "unmapped DATA") != 0) ||
        written) {
        fprintf(stderr, "DATA decoder accepted unmapped address\n");
        return 1;
    }

    const rvrt_frame_t voltage = voltage_frame(0U, 0U, 0x12345678U);
    written = true;
    if ((expect_status(rvrt_decode_output_frame(&view, &voltage, output,
                                                TEST_DATA_ELEMENTS, &written),
                       RVRT_STATUS_OK, "voltage ignored by DATA") != 0) ||
        written) {
        fprintf(stderr, "DATA decoder accepted voltage frame\n");
        return 1;
    }
    return 0;
}

static int verify_output_sequence(const rvrt_artifact_t *artifact)
{
    rvrt_artifact_output_mapping_view_t view = {0};
    if (expect_artifact_status(
            rvrt_artifact_get_output_mapping_view(artifact, 0U, 0U, &view),
            "sequence mapping view") != 0) {
        return 1;
    }

    rvrt_artifact_runtime_t runtime = {
        .timesteps = 8U,
        .tick_depth = 3U,
        .sync_steps = 10U,
        .decode_mode = RVRT_DECODE_MODE_STREAM,
    };
    rvrt_frame_t frames[13] = {0};
    frames[0] = (rvrt_frame_t){0xE0000000U, 0U};
    frames[1] = (rvrt_frame_t){0U, 0U};
    frames[2] = voltage_frame(0U, 0U, 0x12345678U);
    for (uint32_t timestep = 0U; timestep < runtime.timesteps; ++timestep) {
        frames[3U + timestep] = work_frame_at_timestep(
            TEST_WORK_DATA_HIGH, timestep, timestep, (uint8_t)(timestep + 1U));
    }
    frames[11] = work_frame_at_timestep(TEST_WORK_DATA_HIGH, 8U, 0U, 0x55U);
    frames[12] = work_frame_at_timestep(TEST_WORK_DATA_HIGH, 3U,
                                        TEST_DATA_ELEMENTS, 0x66U);

    uint8_t output[8U * TEST_DATA_ELEMENTS];
    memset(output, 0xA5, sizeof(output));
    if (expect_status(
            rvrt_decode_output_frames(&view, &runtime, frames,
                                      (uint32_t)TEST_ARRAY_SIZE(frames), output,
                                      sizeof(output)),
            RVRT_STATUS_OK, "decode output sequence") != 0) {
        return 1;
    }
    for (uint32_t timestep = 0U; timestep < runtime.timesteps; ++timestep) {
        for (uint32_t elem = 0U; elem < view.element_count; ++elem) {
            const uint8_t expected =
                (elem == timestep) ? (uint8_t)(timestep + 1U) : 0U;
            const uint8_t actual = output[timestep * view.element_count + elem];
            if (actual != expected) {
                fprintf(stderr,
                        "sequence mismatch timestep=%u elem=%u got=%u "
                        "expected=%u\n",
                        (unsigned)timestep, (unsigned)elem, (unsigned)actual,
                        (unsigned)expected);
                return 1;
            }
        }
    }

    rvrt_artifact_output_mapping_view_t lcn_view = view;
    lcn_view.target_lcn = 2U;
    const rvrt_artifact_runtime_t lcn_runtime = {
        .timesteps = 2U,
        .tick_depth = 3U,
        .sync_steps = 4U,
        .decode_mode = RVRT_DECODE_MODE_STREAM,
    };
    const rvrt_frame_t lcn_frames[] = {
        work_frame_at_timestep(TEST_WORK_DATA_HIGH, 0U, 0U, 3U),
        work_frame_at_timestep(TEST_WORK_DATA_HIGH, 4U, 1U, 4U),
    };
    uint8_t lcn_output[2U * TEST_DATA_ELEMENTS] = {0};
    if ((expect_status(
             rvrt_decode_output_frames(&lcn_view, &lcn_runtime, lcn_frames,
                                       (uint32_t)TEST_ARRAY_SIZE(lcn_frames),
                                       lcn_output, sizeof(lcn_output)),
             RVRT_STATUS_OK, "target LCN sequence") != 0) ||
        (lcn_output[0] != 3U) || (lcn_output[TEST_DATA_ELEMENTS + 1U] != 4U)) {
        fprintf(stderr, "target LCN application timestep mismatch\n");
        return 1;
    }

    bool written = true;
    const rvrt_frame_t later =
        work_frame_at_timestep(TEST_WORK_DATA_HIGH, 1U, 0U, 0x42U);
    uint8_t single[TEST_DATA_ELEMENTS] = {0};
    if ((expect_status(rvrt_decode_output_frame(&view, &later, single,
                                                TEST_DATA_ELEMENTS, &written),
                       RVRT_STATUS_OK, "legacy decoder later timestep") != 0) ||
        written) {
        fprintf(stderr, "legacy decoder accepted a later timestep\n");
        return 1;
    }

    uint8_t cleared[TEST_DATA_ELEMENTS];
    memset(cleared, 0xA5, sizeof(cleared));
    const rvrt_artifact_runtime_t single_runtime = {
        .timesteps = 1U,
        .tick_depth = 1U,
        .sync_steps = 1U,
        .decode_mode = RVRT_DECODE_MODE_STREAM,
    };
    if ((expect_status(rvrt_decode_output_frames(&view, &single_runtime, NULL,
                                                 0U, cleared, sizeof(cleared)),
                       RVRT_STATUS_OK, "empty output sequence") != 0) ||
        (memcmp(cleared, (uint8_t[TEST_DATA_ELEMENTS]){0}, sizeof(cleared)) !=
         0)) {
        fprintf(stderr, "empty sequence was not cleared\n");
        return 1;
    }

    rvrt_artifact_runtime_t invalid_runtime = runtime;
    rvrt_artifact_output_mapping_view_t invalid_view = view;
    invalid_view.kind = RVRT_OUTPUT_VOLTAGE;
    if (expect_status(
            rvrt_decode_output_frames(&invalid_view, &runtime, frames,
                                      (uint32_t)TEST_ARRAY_SIZE(frames), output,
                                      sizeof(output)),
            RVRT_STATUS_UNSUPPORTED, "VOLTAGE output sequence") != 0) {
        return 1;
    }
    if ((expect_status(rvrt_decode_output_frames(&view, &runtime, NULL, 1U,
                                                 output, sizeof(output)),
                       RVRT_STATUS_NULL_ARGUMENT,
                       "missing frame sequence") != 0) ||
        (expect_status(rvrt_decode_output_frames(NULL, &runtime, NULL, 0U,
                                                 output, sizeof(output)),
                       RVRT_STATUS_NULL_ARGUMENT,
                       "missing output view") != 0)) {
        return 1;
    }
    invalid_runtime.timesteps = 0U;
    if (expect_status(rvrt_decode_output_frames(&view, &invalid_runtime, NULL,
                                                0U, output, sizeof(output)),
                      RVRT_STATUS_BAD_VALUE, "zero output timesteps") != 0) {
        return 1;
    }
    invalid_runtime = runtime;
    invalid_runtime.tick_depth = 0U;
    if (expect_status(rvrt_decode_output_frames(&view, &invalid_runtime, NULL,
                                                0U, output, sizeof(output)),
                      RVRT_STATUS_BAD_VALUE, "zero tick depth") != 0) {
        return 1;
    }
    invalid_runtime = runtime;
    invalid_runtime.decode_mode = RVRT_DECODE_MODE_STEP;
    if (expect_status(
            rvrt_decode_output_frames(&view, &invalid_runtime, frames,
                                      (uint32_t)TEST_ARRAY_SIZE(frames), output,
                                      sizeof(output)),
            RVRT_STATUS_UNSUPPORTED, "STEP output sequence") != 0) {
        return 1;
    }
    invalid_runtime = runtime;
    invalid_runtime.sync_steps--;
    if (expect_status(
            rvrt_decode_output_frames(&view, &invalid_runtime, frames,
                                      (uint32_t)TEST_ARRAY_SIZE(frames), output,
                                      sizeof(output)),
            RVRT_STATUS_BAD_VALUE, "invalid sync metadata") != 0) {
        return 1;
    }
    if (expect_status(
            rvrt_decode_output_frames(&view, &runtime, frames,
                                      (uint32_t)TEST_ARRAY_SIZE(frames), output,
                                      sizeof(output) - 1U),
            RVRT_STATUS_OUT_OF_RANGE, "small output sequence") != 0) {
        return 1;
    }

    rvrt_artifact_output_mapping_view_t oversized = view;
    oversized.element_count = UINT32_MAX;
    invalid_runtime = runtime;
    invalid_runtime.timesteps = 2U;
    invalid_runtime.sync_steps = 4U;
    return expect_status(
        rvrt_decode_output_frames(&oversized, &invalid_runtime, NULL, 0U,
                                  output, sizeof(output)),
        RVRT_STATUS_OUT_OF_RANGE, "output sequence size overflow");
}

static int verify_mnist_sequence(void)
{
    uint8_t input[MNIST_INPUT_TIMESTEPS * MNIST_INPUT_BYTES];
    for (uint32_t sample = 0U; sample < MNIST_SAMPLE_COUNT; ++sample) {
        mnist_build_input(sample, input);
        for (uint32_t timestep = 0U; timestep < MNIST_INPUT_TIMESTEPS;
             ++timestep) {
            uint32_t active_count = 0U;
            for (uint32_t elem = 0U; elem < MNIST_INPUT_BYTES; ++elem) {
                active_count += input[timestep * MNIST_INPUT_BYTES + elem];
            }
            if (active_count != mnist_active_pixel_counts[sample]) {
                fprintf(stderr,
                        "MNIST sample=%u timestep=%u active=%u expected=%u\n",
                        (unsigned)sample, (unsigned)timestep,
                        (unsigned)active_count,
                        (unsigned)mnist_active_pixel_counts[sample]);
                return 1;
            }
        }
    }

    binary_file_t artifact_file = {0};
    rvrt_artifact_t artifact = {0};
    int result = 1;
    if ((read_binary_at(RVRT_MNIST_ASSET_DIR, "compile_artifacts.bin",
                        &artifact_file) != 0) ||
        (expect_artifact_status(rvrt_artifact_read(artifact_file.data,
                                                   artifact_file.size,
                                                   &artifact),
                                "MNIST artifact") != 0)) {
        goto cleanup;
    }

    rvrt_artifact_runtime_t runtime = {0};
    rvrt_artifact_output_mapping_view_t view = {0};
    if ((expect_artifact_status(
             rvrt_artifact_thread_runtime(&artifact, 0U, &runtime),
             "MNIST runtime") != 0) ||
        (expect_artifact_status(
             rvrt_artifact_get_output_mapping_view(&artifact, 0U, 0U, &view),
             "MNIST output mapping") != 0)) {
        goto cleanup;
    }
    if ((runtime.timesteps != 8U) || (runtime.tick_depth != 3U) ||
        (runtime.sync_steps != 10U) ||
        (runtime.decode_mode != RVRT_DECODE_MODE_STREAM) ||
        (view.kind != RVRT_OUTPUT_DATA) || (view.element_count != 10U) ||
        (sizeof(mnist_expected_output[0]) !=
         runtime.timesteps * view.element_count)) {
        fprintf(stderr, "MNIST artifact contract mismatch\n");
        goto cleanup;
    }

    for (uint32_t sample = 0U; sample < MNIST_SAMPLE_COUNT; ++sample) {
        rvrt_frame_t frames[82] = {{0U, 0U}};
        uint32_t frame_count = 0U;
        for (uint32_t axon_bit_idx = 0U; axon_bit_idx < view.entry_count;
             ++axon_bit_idx) {
            rvrt_artifact_output_entry_t entry = {0};
            bool found = false;
            if ((expect_artifact_status(rvrt_artifact_output_mapping_find(
                                            &view, axon_bit_idx, &entry,
                                            &found),
                                        "MNIST output entry") != 0) ||
                !found) {
                goto cleanup;
            }
            for (uint32_t timestep = 0U; timestep < runtime.timesteps;
                 ++timestep) {
                const uint8_t value = mnist_expected_output[sample][
                    timestep * view.element_count + entry.elem_idx];
                if (value != 0U) {
                    frames[frame_count++] = work_frame_at_timestep(
                        TEST_WORK_DATA_HIGH, timestep, axon_bit_idx, value);
                }
            }
        }
        frames[frame_count++] = (rvrt_frame_t){0xE0000000U, 0U};

        uint8_t output[80] = {0};
        if ((expect_status(rvrt_decode_output_frames(&view, &runtime, frames,
                                                     frame_count, output,
                                                     sizeof(output)),
                           RVRT_STATUS_OK, "MNIST sequence decode") != 0) ||
            (memcmp(output, mnist_expected_output[sample], sizeof(output)) !=
             0)) {
            fprintf(stderr, "MNIST sample=%u 8x10 sequence mismatch\n",
                    (unsigned)sample);
            goto cleanup;
        }

        uint32_t sums[10] = {0};
        for (uint32_t timestep = 0U; timestep < runtime.timesteps;
             ++timestep) {
            for (uint32_t elem = 0U; elem < view.element_count; ++elem) {
                sums[elem] += output[timestep * view.element_count + elem];
            }
        }
        uint32_t prediction = 0U;
        for (uint32_t elem = 1U; elem < view.element_count; ++elem) {
            if (sums[elem] > sums[prediction]) {
                prediction = elem;
            }
        }
        if (prediction != mnist_expected_labels[sample]) {
            fprintf(stderr, "MNIST sample=%u prediction=%u expected=%u\n",
                    (unsigned)sample, (unsigned)prediction,
                    (unsigned)mnist_expected_labels[sample]);
            goto cleanup;
        }
    }
    result = 0;

cleanup:
    free_binary(&artifact_file);
    return result;
}

static int verify_voltage_mapping(const rvrt_artifact_t *artifact,
                                  rvrt_artifact_output_mapping_view_t *view)
{
    if (expect_artifact_status(
            rvrt_artifact_get_output_mapping_view(artifact, 0U, 0U, view),
            "voltage mapping view") != 0) {
        return 1;
    }
    if ((view->kind != RVRT_OUTPUT_VOLTAGE) ||
        (view->dtype != TEST_DTYPE_INT32) || (view->target_lcn != 0U) ||
        (view->entry_count != TEST_DATA_ELEMENTS) ||
        (view->element_count != TEST_DATA_ELEMENTS)) {
        fprintf(stderr,
                "voltage mapping mismatch kind=%u dtype=%u lcn=%u entries=%u\n",
                (unsigned)view->kind, (unsigned)view->dtype,
                (unsigned)view->target_lcn, (unsigned)view->entry_count);
        return 1;
    }

    for (uint32_t i = 0U; i < TEST_DATA_ELEMENTS; ++i) {
        rvrt_artifact_output_entry_t entry = {0};
        bool found = false;
        if ((expect_artifact_status(
                 rvrt_artifact_output_mapping_find(view, TEST_VOLTAGE_BASES[i],
                                                   &entry, &found),
                 "voltage base lookup") != 0) ||
            !found || (entry.elem_idx != i)) {
            fprintf(stderr, "voltage base mismatch base=%u elem=%u found=%u\n",
                    (unsigned)TEST_VOLTAGE_BASES[i], (unsigned)entry.elem_idx,
                    (unsigned)found);
            return 1;
        }
    }
    return 0;
}

static int
verify_voltage_decode(const rvrt_artifact_output_mapping_view_t *view)
{
    static const uint32_t values[TEST_DATA_ELEMENTS] = {
        0x12345678U, 0xFEDCBA98U, 0U, 0U, 0U, 0U, 0U, 0x80000001U, 0x7F00AA55U,
    };
    static const voltage_event_t events[] = {
        {0U, 2U}, {1U, 3U}, {7U, 1U}, {8U, 0U}, {1U, 0U}, {0U, 0U},
        {8U, 3U}, {7U, 3U}, {7U, 0U}, {1U, 2U}, {0U, 3U}, {8U, 1U},
        {8U, 2U}, {0U, 1U}, {7U, 2U}, {1U, 1U},
    };

    int32_t output[TEST_DATA_ELEMENTS] = {0};
    rvrt_voltage_decode_state_t state[TEST_DATA_ELEMENTS] = {0};
    uint32_t written_count = 0U;
    for (size_t i = 0U; i < TEST_ARRAY_SIZE(events); ++i) {
        const voltage_event_t event = events[i];
        const rvrt_frame_t frame =
            voltage_frame(TEST_VOLTAGE_BASES[event.elem_idx], event.lane,
                          values[event.elem_idx]);
        bool written = false;
        const rvrt_status_t status =
            rvrt_decode_voltage_frame(view, &frame, output, TEST_DATA_ELEMENTS,
                                      state, TEST_DATA_ELEMENTS, &written);
        if (expect_status(status, RVRT_STATUS_OK, "decode voltage") != 0) {
            return 1;
        }
        written_count += written ? 1U : 0U;
    }

    if (written_count != 4U) {
        fprintf(stderr, "voltage writes=%u expected=4\n",
                (unsigned)written_count);
        return 1;
    }
    const uint32_t decoded_indices[] = {0U, 1U, 7U, 8U};
    for (size_t i = 0U; i < TEST_ARRAY_SIZE(decoded_indices); ++i) {
        const uint32_t index = decoded_indices[i];
        if (((uint32_t)output[index] != values[index]) ||
            (state[index].received_mask != 0U)) {
            fprintf(stderr,
                    "voltage output mismatch index=%u got=%08x expected=%08x "
                    "state=%u\n",
                    (unsigned)index, (unsigned)(uint32_t)output[index],
                    (unsigned)values[index],
                    (unsigned)state[index].received_mask);
            return 1;
        }
    }
    return 0;
}

static int
verify_voltage_errors(const rvrt_artifact_output_mapping_view_t *view)
{
    int32_t output[TEST_DATA_ELEMENTS] = {0};
    rvrt_voltage_decode_state_t state[TEST_DATA_ELEMENTS] = {0};
    const uint32_t value = 0x12345678U;
    const rvrt_frame_t lane2 = voltage_frame(0U, 2U, value);
    bool written = false;
    if ((expect_status(rvrt_decode_voltage_frame(view, &lane2, output,
                                                 TEST_DATA_ELEMENTS, state,
                                                 TEST_DATA_ELEMENTS, &written),
                       RVRT_STATUS_OK, "first voltage lane") != 0) ||
        written) {
        return 1;
    }
    if (expect_status(rvrt_decode_voltage_frame(view, &lane2, output,
                                                TEST_DATA_ELEMENTS, state,
                                                TEST_DATA_ELEMENTS, &written),
                      RVRT_STATUS_BAD_VALUE, "duplicate voltage lane") != 0) {
        return 1;
    }

    rvrt_voltage_decode_state_t partial[TEST_DATA_ELEMENTS] = {0};
    const uint32_t partial_lanes[] = {0U, 2U, 3U};
    for (size_t i = 0U; i < TEST_ARRAY_SIZE(partial_lanes); ++i) {
        const rvrt_frame_t frame =
            voltage_frame(7U, partial_lanes[i], 0x80000001U);
        written = false;
        if ((expect_status(rvrt_decode_voltage_frame(
                               view, &frame, output, TEST_DATA_ELEMENTS,
                               partial, TEST_DATA_ELEMENTS, &written),
                           RVRT_STATUS_OK, "partial voltage") != 0) ||
            written) {
            return 1;
        }
    }
    if ((partial[7].received_mask == 0U) ||
        (partial[7].received_mask == TEST_VOLTAGE_COMPLETE_MASK)) {
        fprintf(stderr, "missing voltage lane was not left partial\n");
        return 1;
    }

    const rvrt_frame_t unmapped = voltage_frame(33U, 0U, value);
    written = true;
    if ((expect_status(rvrt_decode_voltage_frame(view, &unmapped, output,
                                                 TEST_DATA_ELEMENTS, state,
                                                 TEST_DATA_ELEMENTS, &written),
                       RVRT_STATUS_OK, "unmapped voltage") != 0) ||
        written) {
        fprintf(stderr, "voltage decoder accepted unmapped address\n");
        return 1;
    }

    const rvrt_frame_t data_frame = work_frame(TEST_WORK_DATA_HIGH, 0U, 0x78U);
    written = true;
    if ((expect_status(rvrt_decode_voltage_frame(view, &data_frame, output,
                                                 TEST_DATA_ELEMENTS, state,
                                                 TEST_DATA_ELEMENTS, &written),
                       RVRT_STATUS_OK, "DATA ignored by voltage") != 0) ||
        written) {
        fprintf(stderr, "voltage decoder accepted DATA frame\n");
        return 1;
    }

    rvrt_artifact_output_mapping_view_t invalid = *view;
    invalid.kind = RVRT_OUTPUT_DATA;
    written = false;
    if (expect_status(rvrt_decode_voltage_frame(&invalid, &lane2, output,
                                                TEST_DATA_ELEMENTS, state,
                                                TEST_DATA_ELEMENTS, &written),
                      RVRT_STATUS_UNSUPPORTED, "wrong voltage kind") != 0) {
        return 1;
    }
    invalid = *view;
    invalid.dtype = TEST_DTYPE_UINT8;
    return expect_status(
        rvrt_decode_voltage_frame(&invalid, &lane2, output, TEST_DATA_ELEMENTS,
                                  state, TEST_DATA_ELEMENTS, &written),
        RVRT_STATUS_UNSUPPORTED, "wrong voltage dtype");
}

static int verify_managed_packet(void)
{
    static const uint8_t crc_vector[] = {'1', '2', '3', '4', '5',
                                         '6', '7', '8', '9'};
    const uint32_t crc =
        rvrt_packet_crc32(crc_vector, (uint32_t)sizeof(crc_vector));
    if (crc != 0xCBF43926UL) {
        fprintf(stderr, "crc32 mismatch: %08x\n", (unsigned)crc);
        return 1;
    }
    return 0;
}

int main(void)
{
    rv_debug_set_level(RV_DEBUG_ERROR);

    binary_file_t data_file = {0};
    binary_file_t voltage_file = {0};
    rvrt_artifact_t data_artifact = {0};
    rvrt_artifact_t voltage_artifact = {0};
    rvrt_artifact_output_mapping_view_t voltage_view = {0};
    int result = 1;

    if ((read_artifact("compile_artifacts.bin", &data_file, &data_artifact) !=
         0) ||
        (read_artifact("compile_artifacts_voltage.bin", &voltage_file,
                       &voltage_artifact) != 0) ||
        (verify_artifact_basics(&data_artifact, 1U, TEST_DATA_ELEMENTS,
                                TEST_DATA_ELEMENTS + 1U) != 0) ||
        (verify_artifact_basics(&voltage_artifact, 1U,
                                TEST_DATA_ELEMENTS * (uint32_t)sizeof(int32_t),
                                TEST_DATA_ELEMENTS * TEST_VOLTAGE_LANES + 1U) !=
         0) ||
        (verify_control_frames(&data_artifact) != 0) ||
        (verify_input_codec(&data_artifact) != 0) ||
        (verify_data_codec(&data_artifact) != 0) ||
        (verify_output_sequence(&data_artifact) != 0) ||
        (verify_mnist_sequence() != 0) ||
        (verify_voltage_mapping(&voltage_artifact, &voltage_view) != 0) ||
        (verify_voltage_decode(&voltage_view) != 0) ||
        (verify_voltage_errors(&voltage_view) != 0) ||
        (verify_managed_packet() != 0)) {
        goto cleanup;
    }

    printf("runtime codec tests passed\n");
    result = 0;

cleanup:
    free_binary(&voltage_file);
    free_binary(&data_file);
    return result;
}
