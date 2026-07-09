#ifndef RV_NN_QUANT_H
#define RV_NN_QUANT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimum activation scale, matching the PyTorch runtime clamp_min(1e-12). */
#define RV_QUANTIZE_MIN_SCALE 1e-12f

/* Symmetric fp32 -> int8 activation quantization matching the SNN Head
 * PyTorch runtime:
 *
 *     q[i] = clamp(round(x[i] / scale), -127, 127)
 *
 * Notes for numerical parity:
 *   - `scale` is a scalar activation_scale (per tensor), not a vector;
 *   - `scale` is clamped to at least 1e-12, matching quantize_symmetric_int8;
 *   - rounding uses rintf(), i.e. round-to-nearest-even under the default
 *     FE_TONEAREST mode, matching torch.round on .5 ties;
 *   - the output range is [-127, 127], not [-128, 127], to preserve symmetric
 *     zero-point-free quantization.
 *
 * If x or q is NULL, or n == 0, the function returns without writing.
 * In-place is not applicable because input and output element types differ.
 */
void rv_quantize_s8(const float *x, int8_t *q, size_t n, float scale);

/* Per-channel int32 -> fp32 dequantization matching QuantizedLinearRuntime:
 *
 *     y[i] = (float)acc[i] * output_scale[i]
 *
 * Notes:
 *   - `acc` is the int32 accumulator / membrane value;
 *   - `output_scale` is a per-output-channel fp32 vector of length `n`;
 *   - this function does not round or clamp; it only restores fp32 values;
 *   - used for li_out.mem_int32 -> fp32 before fc3 input quantization, and
 *     for fc3.acc_int32 -> final action_float.
 *
 * If acc, y, or output_scale is NULL, or n == 0, the function returns without
 * writing.
 */
void rv_dequantize_s32(const int32_t *acc, float *y, size_t n,
                       const float *output_scale);

#ifdef __cplusplus
}
#endif

#endif /* RV_NN_QUANT_H */
