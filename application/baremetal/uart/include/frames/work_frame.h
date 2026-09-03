#include <stdint.h>

#define HAS_WORK_FRAME 1
const uint32_t work_frame[] __attribute__((section(".large_const_data"))) = {
    0x80000800, 0x00000001,
};
