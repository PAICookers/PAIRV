#include <stdint.h>
#include <stdio.h>

#define ASSET(name)                                                            \
    uint8_t snn_head_##name##_artifact_start[SIZE_##name]                      \
        __attribute__((aligned(16)))
ASSET(fc1_lif);
ASSET(block0_lif);
ASSET(block1_lif);
ASSET(fc2);
ASSET(fc3);

static int load(const char *name, uint8_t *data, size_t bytes)
{
    char path[2048];
    const int n = snprintf(path, sizeof(path), "%s/%s/compile_artifacts.bin",
                           SNN_HEAD_ASSET_DIR, name);
    if (n < 0 || (size_t)n >= sizeof(path))
        return 1;
    FILE *file = fopen(path, "rb");
    if (!file)
        return 1;
    const size_t count = fread(data, 1, bytes, file);
    const int trailing = fgetc(file);
    fclose(file);
    return count != bytes || trailing != EOF;
}

int load_resident_assets(void)
{
#define LOAD(name)                                                             \
    if (load(#name, snn_head_##name##_artifact_start, SIZE_##name))            \
    return 1
    LOAD(fc1_lif);
    LOAD(block0_lif);
    LOAD(block1_lif);
    LOAD(fc2);
    LOAD(fc3);
    return 0;
}
