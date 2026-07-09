#include "nn_layernorm.h"

#include <math.h>

/* Two-pass fp32 LayerNorm matching torch.nn.LayerNorm.
 *
 * PyTorch's CPU kernel computes per-row (mean, rstd) then applies the affine
 * transform as (x - mean) * rstd * gamma + beta. We reproduce the same formula
 * with single-precision accumulation so the on-device N307FD fp32 datapath
 * matches the exported float32 LayerNorm. */
void rv_layernorm_f32(const float *x, float *y,
                      size_t rows, size_t dim,
                      const float *weight, const float *bias,
                      float eps)
{
    if (x == NULL || y == NULL || dim == 0) {
        return;
    }

    for (size_t r = 0; r < rows; ++r) {
        const float *xr = x + r * dim;
        float *yr = y + r * dim;

        /* Pass 1: mean = sum(x) / N. */
        float sum = 0.0f;
        for (size_t i = 0; i < dim; ++i) {
            sum += xr[i];
        }
        float mean = sum / (float)dim;

        /* Pass 2: biased variance = sum((x - mean)^2) / N (divide by N). */
        float var_sum = 0.0f;
        for (size_t i = 0; i < dim; ++i) {
            float d = xr[i] - mean;
            var_sum += d * d;
        }
        float var = var_sum / (float)dim;

        /* rstd = 1 / sqrt(var + eps): eps is added inside the sqrt. */
        float rstd = 1.0f / sqrtf(var + eps);

        /* Affine transform: (x - mean) * rstd * gamma + beta. */
        for (size_t i = 0; i < dim; ++i) {
            float norm = (xr[i] - mean) * rstd;
            float g = (weight != NULL) ? weight[i] : 1.0f;
            float b = (bias != NULL) ? bias[i] : 0.0f;
            yr[i] = norm * g + b;
        }
    }
}

void rv_layernorm_f32_row(const float *x, float *y, size_t dim,
                          const float *weight, const float *bias)
{
    rv_layernorm_f32(x, y, 1, dim, weight, bias, RV_LAYERNORM_DEFAULT_EPS);
}
