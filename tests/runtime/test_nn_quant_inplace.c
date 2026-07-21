#include "nn_quant.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SNN_TIMESTEPS 8U
#define SNN_INPUT_DIM 768U
#define SNN_ACTION_DIM 7U
#define SNN_FC1_INPUT_ELEMENTS (SNN_TIMESTEPS * SNN_INPUT_DIM)
#define SNN_FC3_ACC_ELEMENTS (SNN_TIMESTEPS * SNN_ACTION_DIM)

/* 使用 INT8 QAT 导出包中的真实 scale。 */
#define SNN_FC1_ACTIVATION_SCALE 0.03557577729225159f

static const float SNN_FC3_OUTPUT_SCALE[SNN_ACTION_DIM] = {
    2.786532377e-06f, 3.196316584e-06f, 3.237295005e-06f, 2.417726819e-06f,
    2.554321327e-06f, 2.718235010e-06f, 5.381832125e-06f,
};

static float test_input_value(size_t idx)
{
    const size_t t = idx / SNN_INPUT_DIM;
    const size_t d = idx % SNN_INPUT_DIM;

    /* 构造覆盖正负、上下裁剪和 .5 舍入边界的 fp32 输入。 */
    switch (d % 10U) {
    case 0U:
        return ((float)(int)(d % 97U) - 48.0f) * SNN_FC1_ACTIVATION_SCALE;
    case 1U:
        return ((float)(int)(d % 53U) - 26.0f) * 0.5f * SNN_FC1_ACTIVATION_SCALE;
    case 2U:
        return 200.0f * SNN_FC1_ACTIVATION_SCALE;
    case 3U:
        return -200.0f * SNN_FC1_ACTIVATION_SCALE;
    case 4U:
        return (float)((int)t - 4) * 0.125f;
    case 5U:
        return (float)((int)(d % 17U) - 8) * 0.001953125f;
    case 6U:
        return 0.5f * SNN_FC1_ACTIVATION_SCALE;
    case 7U:
        return -0.5f * SNN_FC1_ACTIVATION_SCALE;
    case 8U:
        return (float)((int)(d % 31U) - 15) * 0.00390625f;
    default:
        return (float)((int)(t + d) - 40) * 0.0009765625f;
    }
}

static int test_quantize_same_buffer(void)
{
    static float input[SNN_FC1_INPUT_ELEMENTS];
    static int8_t expected[SNN_FC1_INPUT_ELEMENTS];
    static uint8_t inplace_buffer[SNN_FC1_INPUT_ELEMENTS * sizeof(float)]
        __attribute__((aligned(4)));

    for (size_t i = 0; i < SNN_FC1_INPUT_ELEMENTS; ++i) {
        input[i] = test_input_value(i);
    }

    rv_quantize_s8(input, expected, SNN_FC1_INPUT_ELEMENTS,
                   SNN_FC1_ACTIVATION_SCALE);

    memcpy(inplace_buffer, input, sizeof(input));
    rv_quantize_s8((const float *)inplace_buffer,
                   (int8_t *)inplace_buffer,
                   SNN_FC1_INPUT_ELEMENTS,
                   SNN_FC1_ACTIVATION_SCALE);

    const int8_t *const actual = (const int8_t *)inplace_buffer;
    for (size_t i = 0; i < SNN_FC1_INPUT_ELEMENTS; ++i) {
        if (actual[i] != expected[i]) {
            printf("FAIL quantize inplace idx=%lu expected=%d actual=%d\r\n",
                   (unsigned long)i, (int)expected[i], (int)actual[i]);
            return 1;
        }
    }

    printf("PASS quantize inplace n=%lu scale=%.10f\r\n",
           (unsigned long)SNN_FC1_INPUT_ELEMENTS,
           (double)SNN_FC1_ACTIVATION_SCALE);
    return 0;
}

static int test_dequantize_same_buffer(void)
{
    static int32_t acc[SNN_FC3_ACC_ELEMENTS];
    static float expected[SNN_FC3_ACC_ELEMENTS];
    static uint8_t inplace_buffer[SNN_FC3_ACC_ELEMENTS * sizeof(int32_t)]
        __attribute__((aligned(4)));

    for (size_t t = 0; t < SNN_TIMESTEPS; ++t) {
        for (size_t o = 0; o < SNN_ACTION_DIM; ++o) {
            const size_t idx = t * SNN_ACTION_DIM + o;
            acc[idx] = (int32_t)((int32_t)t * 1000 - 3500 + (int32_t)o * 137);
            if ((idx % 11U) == 0U) {
                acc[idx] = -acc[idx];
            }
            expected[idx] = (float)acc[idx] * SNN_FC3_OUTPUT_SCALE[o];
        }
    }

    memcpy(inplace_buffer, acc, sizeof(acc));
    rv_dequantize_s32((const int32_t *)inplace_buffer,
                      (float *)inplace_buffer,
                      SNN_TIMESTEPS,
                      SNN_ACTION_DIM,
                      SNN_FC3_OUTPUT_SCALE);

    const float *const actual = (const float *)inplace_buffer;
    for (size_t i = 0; i < SNN_FC3_ACC_ELEMENTS; ++i) {
        if (memcmp(&actual[i], &expected[i], sizeof(float)) != 0) {
            printf("FAIL dequantize inplace idx=%lu expected=%.9g actual=%.9g\r\n",
                   (unsigned long)i, (double)expected[i], (double)actual[i]);
            return 1;
        }
    }

    printf("PASS dequantize inplace n=%lu out_channels=%lu\r\n",
           (unsigned long)SNN_FC3_ACC_ELEMENTS,
           (unsigned long)SNN_ACTION_DIM);
    return 0;
}

int main(void)
{
    int failures = 0;

    printf("nn_quant same-buffer inplace tests\r\n");
    failures += test_quantize_same_buffer();
    failures += test_dequantize_same_buffer();

    if (failures != 0) {
        printf("RESULT: SOME FAILED\r\n");
        return 1;
    }

    printf("RESULT: ALL PASS\r\n");
    return 0;
}
