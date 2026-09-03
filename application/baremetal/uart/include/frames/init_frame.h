#include <stdint.h>

#define HAS_INIT_FRAME 1
const uint32_t init_frame[] __attribute__((section(".large_const_data"))) = {
    0xD0000800, 0x00000000,
};
