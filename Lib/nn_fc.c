#include "nn_fc.h"

void rv_fc_s8_s32(const int8_t *x,
                  const int8_t *weight,
                  const int32_t *bias,
                  int32_t *acc,
                  size_t rows,
                  size_t in_features,
                  size_t out_features)
{
    if (x == NULL || weight == NULL || acc == NULL ||
        rows == 0 || in_features == 0 || out_features == 0) {
        return;
    }

    for (size_t r = 0; r < rows; ++r) {
        const int8_t *xr = x + r * in_features;
        int32_t *ar = acc + r * out_features;

        for (size_t o = 0; o < out_features; ++o) {
            const int8_t *wo = weight + o * in_features;
            int32_t sum = (bias != NULL) ? bias[o] : 0;

            for (size_t i = 0; i < in_features; ++i) {
                sum += (int32_t)xr[i] * (int32_t)wo[i];
            }

            ar[o] = sum;
        }
    }
}

void rv_fc_s8_s32_row(const int8_t *x,
                      const int8_t *weight,
                      const int32_t *bias,
                      int32_t *acc,
                      size_t in_features,
                      size_t out_features)
{
    rv_fc_s8_s32(x, weight, bias, acc, 1, in_features, out_features);
}
