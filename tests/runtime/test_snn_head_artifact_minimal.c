#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "artifact_reader.h"
#include "frame_codec.h"

#define SNN_HEAD_TIMESTEPS 8U
#define SNN_HEAD_TICK_DEPTH 1U
#define SNN_HEAD_DTYPE_UINT1 1U
#define SNN_HEAD_DTYPE_INT32 9U

typedef struct snn_head_artifact_contract_s {
    const char *name;
    const char *path;
    uint32_t input_entries;
    uint32_t input_bit_width;
    uint32_t output_entries;
    uint32_t output_elements;
    uint32_t output_kind;
    uint32_t output_dtype;
} snn_head_artifact_contract_t;

static uint8_t *read_binary_file(const char *path, size_t *size_out)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
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

static int validate_contract(const snn_head_artifact_contract_t *contract)
{
    size_t size = 0U;
    uint8_t *data = read_binary_file(contract->path, &size);
    if (data == NULL) {
        printf("read file failed: %s\n", contract->path);
        return 1;
    }

    rvrt_artifact_t artifact = {0};
    rvrt_artifact_runtime_t runtime = {0};
    rvrt_artifact_input_mapping_view_t input_view = {0};
    rvrt_artifact_output_mapping_view_t output_view = {0};

    if ((rvrt_artifact_read(data, size, &artifact) != RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_thread_runtime(&artifact, 0U, &runtime) != RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_input_mapping_view(&artifact, 0U, 0U, &input_view) != RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_output_mapping_view(&artifact, 0U, 0U, &output_view) != RVRT_ARTIFACT_OK)) {
        free(data);
        printf("%s: artifact minimal read failed\n", contract->name);
        return 2;
    }

    if ((runtime.timesteps != SNN_HEAD_TIMESTEPS) ||
        (runtime.tick_depth != SNN_HEAD_TICK_DEPTH) ||
        (runtime.sync_steps != SNN_HEAD_TIMESTEPS) ||
        (runtime.decode_mode != RVRT_DECODE_MODE_STREAM) ||
        (input_view.entry_count != contract->input_entries) ||
        (input_view.bit_width != contract->input_bit_width) ||
        (output_view.entry_count != contract->output_entries) ||
        (output_view.element_count != contract->output_elements) ||
        (output_view.kind != contract->output_kind) ||
        (output_view.dtype != contract->output_dtype)) {
        printf("%s: artifact contract mismatch\n", contract->name);
        printf("runtime: timesteps=%u tick_depth=%u sync_steps=%u decode_mode=%u\n",
               (unsigned)runtime.timesteps,
               (unsigned)runtime.tick_depth,
               (unsigned)runtime.sync_steps,
               (unsigned)runtime.decode_mode);
        printf("input: entries=%u bit_width=%u\n",
               (unsigned)input_view.entry_count,
               (unsigned)input_view.bit_width);
        printf("output: entries=%u elements=%u kind=%u dtype=%u target_lcn=%u\n",
               (unsigned)output_view.entry_count,
               (unsigned)output_view.element_count,
               (unsigned)output_view.kind,
               (unsigned)output_view.dtype,
               (unsigned)output_view.target_lcn);
        free(data);
        return 3;
    }

    printf("%s: timesteps=%u sync_steps=%u input=%u output=%u elements=%u kind=%u dtype=%u PASS\n",
           contract->name,
           (unsigned)runtime.timesteps,
           (unsigned)runtime.sync_steps,
           (unsigned)input_view.entry_count,
           (unsigned)output_view.entry_count,
           (unsigned)output_view.element_count,
           (unsigned)output_view.kind,
           (unsigned)output_view.dtype);

    free(data);
    return 0;
}

int main(void)
{
    const snn_head_artifact_contract_t contracts[] = {
        {
            "fc1_lif",
            "/mnt/work/linjiamu/VLA/PAIRV-snnhead-riscv-cpu-ops/tests/runtime/fixtures/artifacts2/fc1_lif/runtime/compile_artifacts.bin",
            768U,
            8U,
            1536U,
            1536U,
            RVRT_OUTPUT_DATA,
            SNN_HEAD_DTYPE_UINT1,
        },
        {
            "block0_lif",
            "/mnt/work/linjiamu/VLA/PAIRV-snnhead-riscv-cpu-ops/tests/runtime/fixtures/artifacts2/block0_lif/runtime/compile_artifacts.bin",
            1536U,
            8U,
            1536U,
            1536U,
            RVRT_OUTPUT_DATA,
            SNN_HEAD_DTYPE_UINT1,
        },
        {
            "block1_lif",
            "/mnt/work/linjiamu/VLA/PAIRV-snnhead-riscv-cpu-ops/tests/runtime/fixtures/artifacts2/block1_lif/runtime/compile_artifacts.bin",
            1536U,
            8U,
            1536U,
            1536U,
            RVRT_OUTPUT_DATA,
            SNN_HEAD_DTYPE_UINT1,
        },
        {
            "fc2",
            "/mnt/work/linjiamu/VLA/PAIRV-snnhead-riscv-cpu-ops/tests/runtime/fixtures/artifacts2/fc2/runtime/compile_artifacts.bin",
            1536U,
            8U,
            1536U,
            1536U,
            RVRT_OUTPUT_VOLTAGE,
            SNN_HEAD_DTYPE_INT32,
        },
    };

    for (uint32_t i = 0U; i < (uint32_t)(sizeof(contracts) / sizeof(contracts[0])); ++i) {
        const int result = validate_contract(&contracts[i]);
        if (result != 0) {
            return result;
        }
    }

    printf("SNN_HEAD_ARTIFACT_CONTRACT_PASS\n");
    return 0;
}
