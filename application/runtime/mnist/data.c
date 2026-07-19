#include "data.h"

#include <string.h>

const uint16_t mnist_active_pixels[MNIST_INPUT_ACTIVE_PIXELS] = {
    202, 203, 204, 205, 206, 207, 230, 231, 232, 233, 234, 235, 236, 237, 238,
    239, 240, 241, 242, 243, 244, 245, 258, 259, 260, 261, 262, 263, 264, 265,
    266, 267, 268, 269, 270, 271, 272, 273, 291, 292, 293, 294, 295, 296, 297,
    298, 299, 300, 301, 326, 327, 328, 329, 353, 354, 355, 356, 381, 382, 383,
    384, 408, 409, 410, 411, 436, 437, 438, 439, 463, 464, 465, 466, 491, 492,
    493, 518, 519, 520, 545, 546, 547, 548, 572, 573, 574, 575, 576, 600, 601,
    602, 603, 627, 628, 629, 630, 631, 655, 656, 657, 658, 682, 683, 684, 685,
    686, 710, 711, 712, 713, 714, 738, 739, 740, 741,
};

const uint8_t
    mnist_expected_output[MNIST_INPUT_TIMESTEPS * MNIST_OUTPUT_ELEMENTS] = {
        [7] = 1U,  [17] = 1U, [27] = 1U, [37] = 1U,
        [47] = 1U, [57] = 1U, [67] = 1U, [77] = 1U,
};

void mnist_build_input(uint8_t input[MNIST_INPUT_TIMESTEPS * MNIST_INPUT_BYTES])
{
    if (input == NULL) {
        return;
    }
    memset(input, 0, MNIST_INPUT_TIMESTEPS * MNIST_INPUT_BYTES);
    for (uint32_t timestep = 0U; timestep < MNIST_INPUT_TIMESTEPS; ++timestep) {
        for (uint32_t i = 0U; i < MNIST_INPUT_ACTIVE_PIXELS; ++i) {
            input[timestep * MNIST_INPUT_BYTES + mnist_active_pixels[i]] = 1U;
        }
    }
}
