#ifndef MNIST_RUNTIME_DATA_H
#define MNIST_RUNTIME_DATA_H

#include <stdint.h>

#define MNIST_INPUT_TIMESTEPS 8U
#define MNIST_INPUT_BYTES 784U
#define MNIST_OUTPUT_ELEMENTS 10U
#define MNIST_INPUT_ACTIVE_PIXELS 116U

/*
 * 本样本的源 MNIST 像素未归一化，取值为 0..255。泊松编码器对每个非零像素在每个 timestep
 * 都产生脉冲，因此本 demo 重复发送同一份确定性 UINT1 mask。
 */
extern const uint16_t mnist_active_pixels[MNIST_INPUT_ACTIVE_PIXELS];
extern const uint8_t
    mnist_expected_output[MNIST_INPUT_TIMESTEPS * MNIST_OUTPUT_ELEMENTS];

/** Construct the repeated deterministic UINT1 input sequence for this sample. */
void mnist_build_input(
    uint8_t input[MNIST_INPUT_TIMESTEPS * MNIST_INPUT_BYTES]);

#endif /* MNIST_RUNTIME_DATA_H */
