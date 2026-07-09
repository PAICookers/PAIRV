#include "nn_quant.h"

#include <math.h>

void rv_quantize_s8(const float *x, int8_t *q, size_t n, float scale)
{
    if (x == NULL || q == NULL || n == 0) {
        return;
    }

    float scale_t = scale;
    if (scale_t < RV_QUANTIZE_MIN_SCALE) {
        scale_t = RV_QUANTIZE_MIN_SCALE;
    }

    for (size_t i = 0; i < n; ++i) {
        float v = rintf(x[i] / scale_t);
        if (v > 127.0f) {
            q[i] = 127;
        } else if (v < -127.0f) {
            q[i] = -127;
        } else {
            q[i] = (int8_t)v;
        }
    }
}

void rv_dequantize_s32(const int32_t *acc, float *y, size_t n,
                       const float *output_scale)
{
    if (acc == NULL || y == NULL || output_scale == NULL || n == 0) {
        return;
    }

    for (size_t i = 0; i < n; ++i) {
        y[i] = (float)acc[i] * output_scale[i];
    }
}
