/*
 * Shared host-test helpers for per-layer SNN Head checks.
 *
 * These tests run on a normal host (Linux/x86) with zero PAICORE hardware and
 * zero model parameters. They lock down, per layer, the two things that are
 * verifiable without silicon:
 *
 *   1. The compiled artifact CONTRACT: timing metadata plus the input/output
 *      mapping shapes, output kind (DATA vs VOLTAGE) and dtype. This is exactly
 *      what snn_head_run_<layer>() assumes when it drives the PAICORE session.
 *   2. The input ENCODE frame budget: a dense (all-nonzero) input timestep must
 *      emit exactly input_entries work frames, exercising the same
 *      rvrt_encode_input_chunk() path (including bounded-buffer chunking) that
 *      the layer send loop relies on.
 *
 * The numeric end-to-end behaviour of snn_head_run_<layer>() cannot be checked
 * here: it needs the PAICORE core (or QEMU) and snn_head_params.c. That is a
 * separate on-target verification step.
 */
#ifndef SNN_HEAD_LAYER_TEST_COMMON_H
#define SNN_HEAD_LAYER_TEST_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "artifact_reader.h"
#include "frame_codec.h"

#define SNN_HEAD_TEST_TIMESTEPS 8U
#define SNN_HEAD_TEST_TICK_DEPTH 1U
#define SNN_HEAD_TEST_DTYPE_UINT1 1U
#define SNN_HEAD_TEST_DTYPE_INT32 9U

/* One instance per layer source file describes that layer's PAICORE contract. */
typedef struct snn_head_layer_contract_s {
    const char *name;         /* layer label used in log lines */
    const char *fixture_leaf; /* fixtures2 subdirectory name */
    uint32_t input_entries;   /* input mapping entry_count */
    uint32_t input_bit_width; /* input mapping bit_width */
    uint32_t output_entries;  /* output mapping entry_count */
    uint32_t output_elements; /* output mapping element_count */
    uint32_t output_kind;     /* RVRT_OUTPUT_DATA / RVRT_OUTPUT_VOLTAGE */
    uint32_t output_dtype;    /* SNN_HEAD_TEST_DTYPE_* */
} snn_head_layer_contract_t;

static uint8_t *snn_head_test_read_file(const char *path, size_t *size_out)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if ((fseek(fp, 0, SEEK_END) != 0)) {
        fclose(fp);
        return NULL;
    }
    const long size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    uint8_t *data = (uint8_t *)malloc((size_t)size);
    if (data == NULL) {
        fclose(fp);
        return NULL;
    }
    const size_t read_size = fread(data, 1U, (size_t)size, fp);
    fclose(fp);
    if (read_size != (size_t)size) {
        free(data);
        return NULL;
    }
    *size_out = (size_t)size;
    return data;
}

/* Build the fixture path from the compile-time fixtures directory. */
static int snn_head_test_fixture_path(const snn_head_layer_contract_t *c,
                                      char *buf, size_t buf_size)
{
    const int written = snprintf(buf, buf_size,
                                 "%s/%s/runtime/compile_artifacts.bin",
                                 SNN_HEAD_FIXTURE_DIR, c->fixture_leaf);
    return ((written > 0) && ((size_t)written < buf_size)) ? 0 : 1;
}

/* Validate timing + mapping metadata against the layer contract. */
static int snn_head_test_check_contract(
    const uint8_t *data, size_t size, const snn_head_layer_contract_t *c,
    rvrt_artifact_input_mapping_view_t *input_view_out)
{
    rvrt_artifact_t artifact = {0};
    rvrt_artifact_runtime_t runtime = {0};
    rvrt_artifact_input_mapping_view_t input_view = {0};
    rvrt_artifact_output_mapping_view_t output_view = {0};

    if ((rvrt_artifact_read(data, size, &artifact) != RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_thread_runtime(&artifact, 0U, &runtime) !=
         RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_input_mapping_view(&artifact, 0U, 0U, &input_view) !=
         RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_output_mapping_view(&artifact, 0U, 0U,
                                               &output_view) !=
         RVRT_ARTIFACT_OK)) {
        printf("%s: artifact read failed\n", c->name);
        return 2;
    }

    if ((runtime.timesteps != SNN_HEAD_TEST_TIMESTEPS) ||
        (runtime.tick_depth != SNN_HEAD_TEST_TICK_DEPTH) ||
        (runtime.sync_steps != SNN_HEAD_TEST_TIMESTEPS) ||
        (runtime.decode_mode != RVRT_DECODE_MODE_STREAM) ||
        (input_view.entry_count != c->input_entries) ||
        (input_view.bit_width != c->input_bit_width) ||
        (output_view.entry_count != c->output_entries) ||
        (output_view.element_count != c->output_elements) ||
        (output_view.kind != c->output_kind) ||
        (output_view.dtype != c->output_dtype)) {
        printf("%s: contract mismatch\n", c->name);
        printf("  runtime: timesteps=%u tick_depth=%u sync_steps=%u "
               "decode_mode=%u\n",
               (unsigned)runtime.timesteps, (unsigned)runtime.tick_depth,
               (unsigned)runtime.sync_steps, (unsigned)runtime.decode_mode);
        printf("  input:  entries=%u bit_width=%u (want %u/%u)\n",
               (unsigned)input_view.entry_count, (unsigned)input_view.bit_width,
               (unsigned)c->input_entries, (unsigned)c->input_bit_width);
        printf("  output: entries=%u elements=%u kind=%u dtype=%u "
               "(want %u/%u/%u/%u)\n",
               (unsigned)output_view.entry_count,
               (unsigned)output_view.element_count, (unsigned)output_view.kind,
               (unsigned)output_view.dtype, (unsigned)c->output_entries,
               (unsigned)c->output_elements, (unsigned)c->output_kind,
               (unsigned)c->output_dtype);
        return 3;
    }

    *input_view_out = input_view;
    return 0;
}

/*
 * Encode one dense input timestep and assert the emitted frame count equals
 * input_entries. A small frame buffer forces the BUFFER_FULL chunking path so
 * the accounting is exercised the same way the layer send loop drains it.
 */
static int snn_head_test_check_encode(
    const rvrt_artifact_input_mapping_view_t *input_view,
    const snn_head_layer_contract_t *c)
{
    if (c->input_bit_width != 8U) {
        printf("%s: encode check skipped (bit_width=%u, expected 8)\n", c->name,
               (unsigned)c->input_bit_width);
        return 0;
    }

    const size_t input_size = (size_t)c->input_entries;
    uint8_t *input = (uint8_t *)malloc(input_size);
    if (input == NULL) {
        printf("%s: encode input alloc failed\n", c->name);
        return 4;
    }
    memset(input, 0xFF, input_size); /* dense: every entry emits one frame */

    rvrt_frame_t frames[128];
    rvrt_input_cursor_t cursor;
    rvrt_input_cursor_init(&cursor, 0U);

    uint32_t total_frames = 0U;
    for (;;) {
        uint32_t frame_count = 0U;
        const rvrt_status_t st = rvrt_encode_input_chunk(
            input_view, &cursor, input, input_size, frames,
            (uint32_t)(sizeof(frames) / sizeof(frames[0])), &frame_count);
        total_frames += frame_count;
        if (st == RVRT_STATUS_DONE) {
            break;
        }
        if (st == RVRT_STATUS_BUFFER_FULL) {
            continue;
        }
        printf("%s: encode failed: %s\n", c->name, rvrt_status_string(st));
        free(input);
        return 5;
    }
    free(input);

    if (total_frames != c->input_entries) {
        printf("%s: encode frame budget mismatch: got %u want %u\n", c->name,
               (unsigned)total_frames, (unsigned)c->input_entries);
        return 6;
    }
    return 0;
}

/*
 * Standard per-layer entry point.
 *
 * When skip_if_missing is nonzero and the fixture file is absent, the test
 * prints a PENDING line and returns 0 (used by fc3 until its fixture lands).
 * Otherwise a missing fixture is a hard failure.
 */
static int snn_head_run_layer_test(const snn_head_layer_contract_t *c,
                                   int skip_if_missing)
{
    char path[512];
    if (snn_head_test_fixture_path(c, path, sizeof(path)) != 0) {
        printf("%s: fixture path too long\n", c->name);
        return 1;
    }

    size_t size = 0U;
    uint8_t *data = snn_head_test_read_file(path, &size);
    if (data == NULL) {
        if (skip_if_missing != 0) {
            printf("%s: PENDING (fixture not found: %s)\n", c->name, path);
            return 0;
        }
        printf("%s: read file failed: %s\n", c->name, path);
        return 1;
    }

    rvrt_artifact_input_mapping_view_t input_view = {0};
    int rc = snn_head_test_check_contract(data, size, c, &input_view);
    if (rc == 0) {
        rc = snn_head_test_check_encode(&input_view, c);
    }
    free(data);

    if (rc == 0) {
        printf("%s: contract + encode PASS (input=%u %ubit, output=%u/%u "
               "kind=%u dtype=%u)\n",
               c->name, (unsigned)c->input_entries, (unsigned)c->input_bit_width,
               (unsigned)c->output_entries, (unsigned)c->output_elements,
               (unsigned)c->output_kind, (unsigned)c->output_dtype);
    }
    return rc;
}

#endif /* SNN_HEAD_LAYER_TEST_COMMON_H */
