#include "nn_layernorm.h"

#include <math.h>

/* Two-pass fp32 LayerNorm with fixed four-lane reductions.
 *
 * Four independent partial sums limit each fp32 accumulation chain to roughly
 * one quarter of the row without compensated or double-precision arithmetic.
 * The formula matches torch.nn.LayerNorm; the fixed reduction order is an
 * implementation detail and is not bit-exact with every PyTorch CPU backend.
 */
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

        /* Pass 1: mean = sum(x) / N. Keep the reduction order deterministic. */
        float sum0 = 0.0f;
        float sum1 = 0.0f;
        float sum2 = 0.0f;
        float sum3 = 0.0f;
        size_t i = 0U;
        for (; dim - i >= 4U; i += 4U) {
            sum0 += xr[i];
            sum1 += xr[i + 1U];
            sum2 += xr[i + 2U];
            sum3 += xr[i + 3U];
        }
        for (; i < dim; ++i) {
            sum0 += xr[i];
        }
        const float sum = (sum0 + sum1) + (sum2 + sum3);
        const float mean = sum / (float)dim;

        /* Pass 2: biased variance = sum((x - mean)^2) / N. */
        float var_sum0 = 0.0f;
        float var_sum1 = 0.0f;
        float var_sum2 = 0.0f;
        float var_sum3 = 0.0f;
        i = 0U;
        for (; dim - i >= 4U; i += 4U) {
            const float d0 = xr[i] - mean;
            const float d1 = xr[i + 1U] - mean;
            const float d2 = xr[i + 2U] - mean;
            const float d3 = xr[i + 3U] - mean;
            var_sum0 += d0 * d0;
            var_sum1 += d1 * d1;
            var_sum2 += d2 * d2;
            var_sum3 += d3 * d3;
        }
        for (; i < dim; ++i) {
            const float d = xr[i] - mean;
            var_sum0 += d * d;
        }
        const float var_sum =
            (var_sum0 + var_sum1) + (var_sum2 + var_sum3);
        const float variance = var_sum / (float)dim;

        /* rstd = 1 / sqrt(var + eps): eps is added inside the sqrt. */
        const float rstd = 1.0f / sqrtf(variance + eps);

        /* Affine transform: (x - mean) * rstd * gamma + beta. */
        for (size_t i = 0; i < dim; ++i) {
            const float norm = (xr[i] - mean) * rstd;
            const float g = (weight != NULL) ? weight[i] : 1.0f;
            const float b = (bias != NULL) ? bias[i] : 0.0f;
            yr[i] = norm * g + b;
        }
    }
}

void rv_layernorm_f32_row(const float *x, float *y, size_t dim,
                          const float *weight, const float *bias)
{
    rv_layernorm_f32(x, y, 1, dim, weight, bias, RV_LAYERNORM_DEFAULT_EPS);
}
