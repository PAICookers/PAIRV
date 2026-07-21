#include "nn_quant.h"

#include <math.h>
#include <string.h>

static float sanitize_scale(float scale)
{
    return (scale < RV_QUANTIZE_MIN_SCALE) ? RV_QUANTIZE_MIN_SCALE : scale;
}

static int8_t quantize_s8_value(float x, float scale)
{
    float v = rintf(x / scale);
    if (v > 127.0f) {
        return 127;
    }
    if (v < -127.0f) {
        return -127;
    }
    return (int8_t)v;
}

void rv_quantize_s8(const float *x, int8_t *q, size_t n, float scale)
{
    if (x == NULL || q == NULL || n == 0) {
        return;
    }

    const float scale_t = sanitize_scale(scale);

    for (size_t i = 0; i < n; ++i) {
        q[i] = quantize_s8_value(x[i], scale_t);
    }
}

void rv_dequantize_s32(const int32_t *acc, float *y, size_t rows, size_t cols,
                       const float *output_scale)
{
    if (acc == NULL || y == NULL || output_scale == NULL || rows == 0 || cols == 0) {
        return;
    }

    for (size_t r = 0; r < rows; ++r) {
        const size_t row_base = r * cols;
        for (size_t c = 0; c < cols; ++c) {
            y[row_base + c] = (float)acc[row_base + c] * output_scale[c];
        }
    }
}
