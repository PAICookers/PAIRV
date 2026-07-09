#ifndef RV_NN_LAYERNORM_H
#define RV_NN_LAYERNORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default epsilon, matching PyTorch nn.LayerNorm default (eps = 1e-5). */
#define RV_LAYERNORM_DEFAULT_EPS 1e-5f

/* fp32 LayerNorm over the last dimension, numerically matching
 * torch.nn.LayerNorm(dim, eps, elementwise_affine=True).
 *
 * For each of `rows` independent rows of length `dim`:
 *     mean = (1/dim) * sum_i x[i]
 *     var  = (1/dim) * sum_i (x[i] - mean)^2        // biased (divide by N)
 *     y[i] = (x[i] - mean) / sqrt(var + eps) * weight[i] + bias[i]
 *
 * Notes to stay identical to PyTorch:
 *   - variance is BIASED (divides by N, not N-1);
 *   - eps is added INSIDE the sqrt (rstd = 1/sqrt(var + eps));
 *   - all math is IEEE-754 single precision (float), matching the CPU fp32
 *     datapath; no bf16 / NICE is involved.
 *
 * weight (gamma) and bias (beta) each have length `dim`. Passing NULL for
 * weight and/or bias selects the elementwise_affine=False behaviour for that
 * term (weight = 1.0, bias = 0.0).
 *
 * `x` and `y` may point to the same buffer (in-place is supported).
 */
void rv_layernorm_f32(const float *x, float *y,
                      size_t rows, size_t dim,
                      const float *weight, const float *bias,
                      float eps);

/* Convenience wrapper: a single row with the PyTorch default eps = 1e-5. */
void rv_layernorm_f32_row(const float *x, float *y, size_t dim,
                          const float *weight, const float *bias);

#ifdef __cplusplus
}
#endif

#endif /* RV_NN_LAYERNORM_H */
