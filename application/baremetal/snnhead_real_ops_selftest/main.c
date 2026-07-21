#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "nn_fc.h"
#include "nn_layernorm.h"
#include "nn_quant.h"
#include "test_data.h"

#define LN_TOL 1e-4f
#define ACTION_TOL 1e-5f

static float g_ln1_y[SNN_REAL_LN1_DIM];
static float g_ln2_y[SNN_REAL_LN2_DIM];
static int8_t g_quant_y[SNN_REAL_LN2_DIM];
static int32_t g_fc3_acc[SNN_REAL_FC3_OUT];
static float g_action[SNN_REAL_FC3_OUT];

static float max_abs_diff(const float *x, const float *y, size_t n, int *finite)
{
    float max_d = 0.0f;
    *finite = 1;
    for (size_t i = 0; i < n; ++i) {
        if (!isfinite(x[i])) {
            *finite = 0;
        }
        const float d = fabsf(x[i] - y[i]);
        if (d > max_d) {
            max_d = d;
        }
    }
    return max_d;
}

static int check_layernorms(void)
{
    int finite = 1;
    int ok = 1;

    rv_layernorm_f32(SNN_LN1_X, g_ln1_y, 1U, SNN_REAL_LN1_DIM,
                     SNN_LN1_WEIGHT, SNN_LN1_BIAS, RV_LAYERNORM_DEFAULT_EPS);
    float max_d = max_abs_diff(g_ln1_y, SNN_LN1_Y_REF, SNN_REAL_LN1_DIM, &finite);
    int case_ok = finite && (max_d <= LN_TOL);
    ok &= case_ok;
    printf("[%s] LN1 dim=%lu max_abs_diff=%ld e-9 finite=%d\r\n",
           case_ok ? "PASS" : "FAIL", (unsigned long)SNN_REAL_LN1_DIM,
           (long)(max_d * 1e9f), finite);

    rv_layernorm_f32(SNN_LN2_X, g_ln2_y, 1U, SNN_REAL_LN2_DIM,
                     SNN_LN2_WEIGHT, SNN_LN2_BIAS, RV_LAYERNORM_DEFAULT_EPS);
    max_d = max_abs_diff(g_ln2_y, SNN_LN2_Y_REF, SNN_REAL_LN2_DIM, &finite);
    case_ok = finite && (max_d <= LN_TOL);
    ok &= case_ok;
    printf("[%s] LN2 dim=%lu max_abs_diff=%ld e-9 finite=%d\r\n",
           case_ok ? "PASS" : "FAIL", (unsigned long)SNN_REAL_LN2_DIM,
           (long)(max_d * 1e9f), finite);

    return ok;
}

static int check_quantize_real_size(void)
{
    int ok = 1;
    int mismatch = 0;

    rv_quantize_s8(SNN_QUANT_X, g_quant_y, SNN_REAL_LN2_DIM,
                   SNN_REAL_FC3_ACTIVATION_SCALE);

    for (size_t i = 0; i < SNN_REAL_LN2_DIM; ++i) {
        if (g_quant_y[i] != SNN_QUANT_REF[i]) {
            ok = 0;
            mismatch++;
            if (mismatch <= 8) {
                printf("  quant mismatch i=%lu got=%d ref=%d\r\n",
                       (unsigned long)i, (int)g_quant_y[i], (int)SNN_QUANT_REF[i]);
            }
        }
    }

    printf("[%s] quant dim=%lu scale=%ld e-9 mismatches=%d\r\n",
           ok ? "PASS" : "FAIL", (unsigned long)SNN_REAL_LN2_DIM,
           (long)(SNN_REAL_FC3_ACTIVATION_SCALE * 1e9f), mismatch);
    return ok;
}

static int check_fc3_real_size(void)
{
    int ok = 1;
    int mismatch = 0;

    rv_fc_s8_s32(g_quant_y, SNN_FC3_WEIGHT, SNN_FC3_BIAS, g_fc3_acc,
                 1U, SNN_REAL_FC3_IN, SNN_REAL_FC3_OUT);

    for (size_t i = 0; i < SNN_REAL_FC3_OUT; ++i) {
        if (g_fc3_acc[i] != SNN_FC3_ACC_REF[i]) {
            ok = 0;
            mismatch++;
            printf("  fc3 mismatch o=%lu got=%ld ref=%ld\r\n",
                   (unsigned long)i, (long)g_fc3_acc[i], (long)SNN_FC3_ACC_REF[i]);
        }
    }

    printf("[%s] fc3 rows=1 in=%lu out=%lu macs=%lu mismatches=%d\r\n",
           ok ? "PASS" : "FAIL", (unsigned long)SNN_REAL_FC3_IN,
           (unsigned long)SNN_REAL_FC3_OUT,
           (unsigned long)(SNN_REAL_FC3_IN * SNN_REAL_FC3_OUT), mismatch);
    return ok;
}

static int check_dequantize_action(void)
{
    int finite = 1;
    rv_dequantize_s32(g_fc3_acc, g_action, 1U, SNN_REAL_FC3_OUT,
                      SNN_FC3_OUTPUT_SCALE);

    const float max_d = max_abs_diff(g_action, SNN_ACTION_REF, SNN_REAL_FC3_OUT, &finite);
    const int ok = finite && (max_d <= ACTION_TOL);

    printf("[%s] action dequant out=%lu max_abs_diff=%ld e-9 finite=%d\r\n",
           ok ? "PASS" : "FAIL", (unsigned long)SNN_REAL_FC3_OUT,
           (long)(max_d * 1e9f), finite);
    return ok;
}

int main(void)
{
    int all_ok = 1;

    printf("SNN Head real-size ops selftest (N307FD/QEMU)\r\n");
    printf("Package dims: LN1=%lu LN2=%lu fc3=%lu->%lu\r\n",
           (unsigned long)SNN_REAL_LN1_DIM, (unsigned long)SNN_REAL_LN2_DIM,
           (unsigned long)SNN_REAL_FC3_IN, (unsigned long)SNN_REAL_FC3_OUT);

    all_ok &= check_layernorms();
    all_ok &= check_quantize_real_size();
    all_ok &= check_fc3_real_size();
    all_ok &= check_dequantize_action();

    printf("RESULT: %s\r\n", all_ok ? "ALL PASS" : "SOME FAILED");
    return all_ok ? 0 : 1;
}
