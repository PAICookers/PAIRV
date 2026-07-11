#include <stdint.h>
#include <stdio.h>

#include "artifact_reader.h"

extern const uint8_t rvrt_fb_artifact_start[];
extern const uint8_t rvrt_fb_artifact_size[];

#define APP_TITLE "flatbuffers"

static uint32_t binary_size(const uint8_t *size_symbol)
{
    return (uint32_t)(uintptr_t)size_symbol;
}

int main(void)
{
    const uint8_t *const artifact_data = rvrt_fb_artifact_start;
    const uint32_t artifact_size = binary_size(rvrt_fb_artifact_size);
    rvrt_artifact_t artifact = {0};
    rvrt_artifact_info_t info = {0};

    rvrt_artifact_status_t status =
        rvrt_artifact_read(artifact_data, artifact_size, &artifact);
    if (status != RVRT_ARTIFACT_OK) {
        printf("%s: artifact read failed: %s\r\n", APP_TITLE,
               rvrt_artifact_status_string(status));
        return 1;
    }

    status = rvrt_artifact_get_info(&artifact, &info);
    if (status != RVRT_ARTIFACT_OK) {
        printf("%s: artifact info failed: %s\r\n", APP_TITLE,
               rvrt_artifact_status_string(status));
        return 2;
    }

    printf("%s: bytes=%u\r\n", APP_TITLE, (unsigned int)artifact_size);
    printf(
        "schema_version=%u config_words=%u threads=%u word_order=%u\r\n",
        (unsigned int)info.schema_version, (unsigned int)info.config_word_count,
        (unsigned int)info.thread_count, (unsigned int)info.config_word_order);

    if (info.config_word_count >= 2U) {
        uint32_t high = 0U;
        uint32_t low = 0U;
        status = rvrt_artifact_config_frame_words(&artifact, 0U, &high, &low);
        if (status != RVRT_ARTIFACT_OK) {
            printf("%s: first config frame failed: %s\r\n", APP_TITLE,
                   rvrt_artifact_status_string(status));
            return 3;
        }
        printf("first_config_frame=0x%08X%08X\r\n", (unsigned int)high,
               (unsigned int)low);
    }

    printf("FLATBUFFER_READ_PASS\r\n");
    return 0;
}
