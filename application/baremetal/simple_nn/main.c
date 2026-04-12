#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "riscv_nnfunctions.h"

static void print_vector(const char *label, const int8_t *data, uint32_t size)
{
    printf("%s:", label);
    for (uint32_t i = 0; i < size; ++i) {
        printf(" %d", data[i]);
    }
    printf("\r\n");
}

static bool run_conv1x1_s8(const int8_t *input_data, const int8_t *weights,
                           const int32_t *bias_data, int8_t *output_data)
{
    uint8_t scratch[16] = {0};
    const nmsis_nn_context ctx = {.buf = scratch,
                                  .size = (int32_t)sizeof(scratch)};
    int32_t multipliers[2] = {1 << 30, 1 << 30};
    int32_t shifts[2] = {1, 1};
    const nmsis_nn_per_channel_quant_params quant_params = {
        .multiplier = multipliers, .shift = shifts};
    const nmsis_nn_conv_params conv_params = {
        .input_offset = 0,
        .output_offset = 0,
        .stride = {.w = 1, .h = 1},
        .padding = {.w = 0, .h = 0},
        .dilation = {.w = 1, .h = 1},
        .activation = {.min = -128, .max = 127},
    };
    const nmsis_nn_dims input_dims = {.n = 1, .h = 3, .w = 3, .c = 1};
    const nmsis_nn_dims filter_dims = {.n = 2, .h = 1, .w = 1, .c = 1};
    const nmsis_nn_dims bias_dims = {.n = 1, .h = 1, .w = 1, .c = 2};
    const nmsis_nn_dims output_dims = {.n = 1, .h = 3, .w = 3, .c = 2};
    const int32_t required = riscv_convolve_wrapper_s8_get_buffer_size(
        &conv_params, &input_dims, &filter_dims, &output_dims);
    riscv_nmsis_nn_status status;

    if (required > ctx.size) {
        printf("conv scratch too small: need %ld have %ld\r\n", (long)required,
               (long)ctx.size);
        return false;
    }

    status = riscv_convolve_wrapper_s8(&ctx, &conv_params, &quant_params,
                                       &input_dims, input_data, &filter_dims,
                                       weights, &bias_dims, bias_data,
                                       &output_dims, output_data);

    if (status != RISCV_NMSIS_NN_SUCCESS) {
        printf("conv failed with status %d\r\n", status);
        return false;
    }

    return true;
}

static bool run_fc_s8(const int8_t *input_data, uint32_t input_size,
                      const int8_t *weights, uint32_t output_size,
                      const int32_t *bias_data, int8_t *output_data)
{
    const nmsis_nn_context ctx = {.buf = NULL, .size = 0};
    const nmsis_nn_fc_params fc_params = {
        .input_offset = 0,
        .filter_offset = 0,
        .output_offset = 0,
        .activation = {.min = -128, .max = 127}};
    const nmsis_nn_per_tensor_quant_params quant_params = {
        .multiplier = 1 << 30, .shift = 1};
    const nmsis_nn_dims input_dims = {
        .n = 1, .h = 1, .w = 1, .c = (int32_t)input_size};
    const nmsis_nn_dims filter_dims = {
        .n = (int32_t)input_size, .h = 1, .w = 1, .c = (int32_t)output_size};
    const nmsis_nn_dims bias_dims = {
        .n = 1, .h = 1, .w = 1, .c = (int32_t)output_size};
    const nmsis_nn_dims output_dims = {
        .n = 1, .h = 1, .w = 1, .c = (int32_t)output_size};
    const riscv_nmsis_nn_status status = riscv_fully_connected_s8(
        &ctx, &fc_params, &quant_params, &input_dims, input_data, &filter_dims,
        weights, &bias_dims, bias_data, &output_dims, output_data);

    if (status != RISCV_NMSIS_NN_SUCCESS) {
        printf("linear failed with status %d\r\n", status);
        return false;
    }

    return true;
}

int main(void)
{
    const int8_t input[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    const int8_t conv_weights[] = {1, -1};
    const int32_t conv_bias[] = {0, 8};
    const int8_t linear_weights[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
                                     1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1,
                                     0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
    const int32_t linear_bias[] = {0, 0};
    const int8_t expected_relu[] = {1, 6, 2, 6, 3, 5, 4, 4, 5,
                                    3, 6, 2, 6, 1, 6, 0, 6, 0};
    const int8_t expected_linear[] = {39, 27};
    int8_t conv_output[18] = {0};
    int8_t linear_output[2] = {0};
    bool ok = true;

    print_vector("input", input, 9);
    ok &= run_conv1x1_s8(input, conv_weights, conv_bias, conv_output);
    print_vector("conv", conv_output, 18);
    riscv_relu6_s8(conv_output, 18);
    print_vector("relu6", conv_output, 18);
    ok &= run_fc_s8(conv_output, 18, linear_weights, 2, linear_bias,
                    linear_output);
    print_vector("linear", linear_output, 2);

    for (uint32_t i = 0; i < 18; ++i) {
        if (conv_output[i] != expected_relu[i]) {
            printf("relu mismatch at %lu: got %d expected %d\r\n",
                   (unsigned long)i, conv_output[i], expected_relu[i]);
            ok = false;
        }
    }

    for (uint32_t i = 0; i < 2; ++i) {
        if (linear_output[i] != expected_linear[i]) {
            printf("linear mismatch at %lu: got %d expected %d\r\n",
                   (unsigned long)i, linear_output[i], expected_linear[i]);
            ok = false;
        }
    }

    printf("simple_nn %s\r\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
