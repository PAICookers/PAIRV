#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "artifact_reader.h"
#include "frame_codec.h"

#ifndef SNN_HEAD_ASSET_DIR
#define SNN_HEAD_ASSET_DIR "application/baremetal/snn_head/assets"
#endif

#define SNN_HEAD_TIMESTEPS 8U
#define SNN_HEAD_DTYPE_UINT1 1U
#define SNN_HEAD_DTYPE_INT32 9U

typedef struct snn_head_artifact_contract_s {
    const char *name;
    const char *relative_path;
    uint32_t input_entries;
    uint32_t input_bit_width;
    uint32_t output_entries;
    uint32_t output_elements;
    uint32_t output_kind;
    uint32_t output_dtype;
} snn_head_artifact_contract_t;

static int validate_contract(const snn_head_artifact_contract_t *contract)
{
    char path[512];
    const int path_length =
        snprintf(path, sizeof(path), "%s/%s", SNN_HEAD_ASSET_DIR,
                 contract->relative_path);
    if ((path_length < 0) || ((size_t)path_length >= sizeof(path))) {
        return 1;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return 1;
    }
    const long file_size = ftell(file);
    if (file_size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }
    uint8_t *data = (uint8_t *)malloc((size_t)file_size);
    if (data == NULL) {
        fclose(file);
        return 1;
    }
    const size_t read_size = fread(data, 1U, (size_t)file_size, file);
    fclose(file);
    if (read_size != (size_t)file_size) {
        free(data);
        return 1;
    }

    rvrt_artifact_view_t artifact = {0};
    rvrt_artifact_runtime_t runtime = {0};
    rvrt_artifact_input_mapping_view_t input = {0};
    rvrt_artifact_output_mapping_view_t output = {0};
    if ((rvrt_artifact_read(data, (size_t)file_size, &artifact) !=
         RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_thread_runtime(&artifact, 0U, &runtime) !=
         RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_input_mapping_view(&artifact, 0U, 0U, &input) !=
         RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_output_mapping_view(&artifact, 0U, 0U, &output) !=
         RVRT_ARTIFACT_OK)) {
        free(data);
        return 2;
    }

    const int mismatch = (runtime.timesteps != SNN_HEAD_TIMESTEPS) ||
                         (input.entry_count != contract->input_entries) ||
                         (input.bit_width != contract->input_bit_width) ||
                         (output.entry_count != contract->output_entries) ||
                         (output.element_count != contract->output_elements) ||
                         (output.kind != contract->output_kind) ||
                         (output.dtype != contract->output_dtype);
    if (mismatch) {
        printf("%s: artifact contract mismatch\n", contract->name);
        free(data);
        return 3;
    }
    printf("%s: timesteps=%u input=%u output=%u PASS\n", contract->name,
           (unsigned)runtime.timesteps, (unsigned)input.entry_count,
           (unsigned)output.entry_count);
    free(data);
    return 0;
}

int main(void)
{
    const snn_head_artifact_contract_t contracts[] = {
        {"fc1_lif", "fc1_lif/compile_artifacts.bin", 768U, 8U, 1536U, 1536U,
         RVRT_OUTPUT_DATA, SNN_HEAD_DTYPE_UINT1},
        {"block0_lif", "block0_lif/compile_artifacts.bin", 1536U, 8U, 1536U,
         1536U, RVRT_OUTPUT_DATA, SNN_HEAD_DTYPE_UINT1},
        {"block1_lif", "block1_lif/compile_artifacts.bin", 1536U, 8U, 1536U,
         1536U, RVRT_OUTPUT_DATA, SNN_HEAD_DTYPE_UINT1},
        {"fc2", "fc2/compile_artifacts.bin", 1536U, 8U, 1536U, 1536U,
         RVRT_OUTPUT_VOLTAGE, SNN_HEAD_DTYPE_INT32},
    };
    for (uint32_t index = 0U;
         index < (uint32_t)(sizeof(contracts) / sizeof(contracts[0]));
         ++index) {
        if (validate_contract(&contracts[index]) != 0) {
            return 1;
        }
    }
    puts("SNN_HEAD_ARTIFACT_CONTRACT_PASS");
    return 0;
}
