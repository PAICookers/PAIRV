#ifndef SNN_HEAD_GOLDEN_COMPARE_H
#define SNN_HEAD_GOLDEN_COMPARE_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* C and PyTorch both use float32 at these layer boundaries. The tolerance
 * covers only float evaluation-order differences in dequantization. */
#define SNN_HEAD_GOLDEN_F32_ABS_TOLERANCE 1.0e-5f
#define SNN_HEAD_GOLDEN_F32_REL_TOLERANCE 1.0e-5f

static inline uint32_t snn_head_golden_f32_bits(float value)
{
    uint32_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline int snn_head_golden_compare_exact_f32(const char *name,
                                                    const float *actual,
                                                    const float *expected,
                                                    uint32_t count)
{
    for (uint32_t index = 0U; index < count; ++index) {
        if (actual[index] != expected[index]) {
            printf("%s: GOLDEN FAIL idx=%u expected=0x%08lx actual=0x%08lx\r\n",
                   name, (unsigned)index,
                   (unsigned long)snn_head_golden_f32_bits(expected[index]),
                   (unsigned long)snn_head_golden_f32_bits(actual[index]));
            return 1;
        }
    }
    printf("%s: GOLDEN PASS count=%u\r\n", name, (unsigned)count);
    return 0;
}

static inline int snn_head_golden_compare_all_exact_f32(const char *name,
                                                        const float *actual,
                                                        const float *expected,
                                                        uint32_t count)
{
    uint32_t mismatches = 0U;

    for (uint32_t index = 0U; index < count; ++index) {
        if (actual[index] != expected[index]) {
            printf("%s: GOLDEN MISMATCH idx=%u expected=0x%08lx "
                   "actual=0x%08lx\r\n",
                   name, (unsigned)index,
                   (unsigned long)snn_head_golden_f32_bits(expected[index]),
                   (unsigned long)snn_head_golden_f32_bits(actual[index]));
            ++mismatches;
        }
    }
    if (mismatches == 0U) {
        printf("%s: GOLDEN PASS count=%u\r\n", name, (unsigned)count);
        return 0;
    }
    printf("%s: GOLDEN FAIL mismatches=%u count=%u\r\n", name,
           (unsigned)mismatches, (unsigned)count);
    return 1;
}

static inline int snn_head_golden_compare_close_f32(const char *name,
                                                    const float *actual,
                                                    const float *expected,
                                                    uint32_t count)
{
    for (uint32_t index = 0U; index < count; ++index) {
        const float actual_value = actual[index];
        const float expected_value = expected[index];
        const float difference = fabsf(actual_value - expected_value);
        const float tolerance =
            SNN_HEAD_GOLDEN_F32_ABS_TOLERANCE +
            SNN_HEAD_GOLDEN_F32_REL_TOLERANCE * fabsf(expected_value);
        if (!isfinite(actual_value) || !isfinite(expected_value) ||
            (difference > tolerance)) {
            printf("%s: GOLDEN FAIL idx=%u expected=0x%08lx actual=0x%08lx "
                   "diff=0x%08lx tol=0x%08lx\r\n",
                   name, (unsigned)index,
                   (unsigned long)snn_head_golden_f32_bits(expected_value),
                   (unsigned long)snn_head_golden_f32_bits(actual_value),
                   (unsigned long)snn_head_golden_f32_bits(difference),
                   (unsigned long)snn_head_golden_f32_bits(tolerance));
            return 1;
        }
    }
    printf("%s: GOLDEN PASS count=%u abs_tol=1e-5 rel_tol=1e-5\r\n", name,
           (unsigned)count);
    return 0;
}

#endif /* SNN_HEAD_GOLDEN_COMPARE_H */
