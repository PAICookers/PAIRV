#include "data.h"

#include <stddef.h>
#include <string.h>

static const uint16_t mnist_active_pixels_0[MNIST_INPUT_ACTIVE_PIXELS_0] = {
    202, 203, 204, 205, 206, 207, 230, 231, 232, 233, 234, 235, 236, 237,
    238, 239, 240, 241, 242, 243, 244, 245, 258, 259, 260, 261, 262, 263,
    264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 291, 292, 293, 294,
    295, 296, 297, 298, 299, 300, 301, 326, 327, 328, 329, 353, 354, 355,
    356, 381, 382, 383, 384, 408, 409, 410, 411, 436, 437, 438, 439, 463,
    464, 465, 466, 491, 492, 493, 518, 519, 520, 521, 545, 546, 547, 548,
    572, 573, 574, 575, 576, 600, 601, 602, 603, 627, 628, 629, 630, 631,
    655, 656, 657, 658, 682, 683, 684, 685, 686, 710, 711, 712, 713, 714,
    738, 739, 740, 741,
};

static const uint16_t mnist_active_pixels_1[MNIST_INPUT_ACTIVE_PIXELS_1] = {
    94,  95,  96,  97,  98,  99,  100, 121, 122, 123, 124, 125, 126, 127,
    128, 129, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 175, 176,
    177, 178, 179, 180, 182, 183, 184, 185, 203, 204, 205, 206, 210, 211,
    212, 213, 232, 233, 238, 239, 240, 241, 265, 266, 267, 268, 269, 292,
    293, 294, 295, 296, 320, 321, 322, 323, 347, 348, 349, 350, 351, 374,
    375, 376, 377, 378, 402, 403, 404, 405, 429, 430, 431, 432, 433, 456,
    457, 458, 459,
    460, 484, 485, 486, 487, 488, 512, 513, 514, 515, 540, 541, 542, 543,
    544, 545, 546, 547, 548, 550, 551, 552, 553, 554, 555, 556, 557, 558,
    568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581,
    582, 583, 584, 585, 586, 596, 597, 598, 599, 600, 601, 602, 603, 604,
    605, 606, 607, 608, 609, 610, 611, 612, 613, 614, 625, 626, 627, 628,
    629, 630, 631, 632, 633, 634, 635, 636,
};

const uint16_t *const mnist_active_pixels[MNIST_SAMPLE_COUNT] = {
    mnist_active_pixels_0,
    mnist_active_pixels_1,
};

const uint16_t mnist_active_pixel_counts[MNIST_SAMPLE_COUNT] = {
    MNIST_INPUT_ACTIVE_PIXELS_0,
    MNIST_INPUT_ACTIVE_PIXELS_1,
};

const uint8_t mnist_expected_labels[MNIST_SAMPLE_COUNT] = {7U, 2U};

const uint8_t mnist_expected_output
    [MNIST_SAMPLE_COUNT][MNIST_INPUT_TIMESTEPS * MNIST_OUTPUT_ELEMENTS] = {
        {
            [7] = 1U,  [17] = 1U, [27] = 1U, [37] = 1U,
            [47] = 1U, [57] = 1U, [67] = 1U, [77] = 1U,
        },
        {
            [2] = 1U,  [12] = 1U, [22] = 1U, [32] = 1U,
            [42] = 1U, [52] = 1U, [62] = 1U, [72] = 1U,
        },
    };

void mnist_build_input(
    uint32_t sample_index,
    uint8_t input[MNIST_INPUT_TIMESTEPS * MNIST_INPUT_BYTES])
{
    if ((input == NULL) || (sample_index >= MNIST_SAMPLE_COUNT)) {
        return;
    }

    memset(input, 0, MNIST_INPUT_TIMESTEPS * MNIST_INPUT_BYTES);
    const uint16_t *const active_pixels = mnist_active_pixels[sample_index];
    const uint32_t active_count = mnist_active_pixel_counts[sample_index];
    for (uint32_t timestep = 0U; timestep < MNIST_INPUT_TIMESTEPS; ++timestep) {
        for (uint32_t i = 0U; i < active_count; ++i) {
            input[timestep * MNIST_INPUT_BYTES + active_pixels[i]] = 1U;
        }
    }
}
