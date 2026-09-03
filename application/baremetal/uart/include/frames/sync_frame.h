#include <stdint.h>

#define HAS_SYNC_FRAME 1
const uint32_t sync_frame[] __attribute__((section(".large_const_data"))) = {
    0xC0000800, 0x00000001,
};
