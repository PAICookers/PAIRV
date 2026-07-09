#ifndef RV_NN_FC_H
#define RV_NN_FC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generic int8 fully-connected / Linear reference kernel.
 *
 * Matches the accumulator part of PyTorch/runtime Linear quantization:
 *
 *     acc[r, o] = bias[o] + sum_i x[r, i] * weight[o, i]
 *
 * Shapes and layout:
 *   - x:      int8  [rows, in_features], row-major;
 *   - weight: int8  [out_features, in_features], row-major, matching
 *             PyTorch nn.Linear weight layout [out_features, in_features];
 *   - bias:   int32 [out_features], may be NULL (treated as zero);
 *   - acc:    int32 [rows, out_features], row-major output accumulator.
 *
 * This kernel intentionally does NOT perform activation quantization,
 * dequantization, output scaling, or LIF logic. Those are separate steps:
 *   rv_quantize_s8    : fp32 -> int8
 *   rv_fc_s8_s32      : int8 x int8 -> int32 accumulator
 *   rv_dequantize_s32 : int32 -> fp32
 *
 * If x, weight, or acc is NULL, or any dimension is zero, the function returns
 * without writing.
 */
void rv_fc_s8_s32(const int8_t *x,
                  const int8_t *weight,
                  const int32_t *bias,
                  int32_t *acc,
                  size_t rows,
                  size_t in_features,
                  size_t out_features);

/* Convenience wrapper for a single input row. */
void rv_fc_s8_s32_row(const int8_t *x,
                      const int8_t *weight,
                      const int32_t *bias,
                      int32_t *acc,
                      size_t in_features,
                      size_t out_features);

#ifdef __cplusplus
}
#endif

#endif /* RV_NN_FC_H */
