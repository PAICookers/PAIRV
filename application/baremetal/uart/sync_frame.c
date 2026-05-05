#include "frames.h"

const uint32_t sync_frame[SYNC_FRAME_WORDS]
    __attribute__((section(".large_const_data"))) = {
        0b11000000010000000000000000000000, 0b00000000000000000000000000000001};
