#include "nice_primitives.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NICE_RESULT_SAMPLE_COUNT 8U

void print_misa(void)
{
    CSR_MISA_Type misa_bits = (CSR_MISA_Type)__RV_CSR_READ(CSR_MISA);
    static char misa_chars[30];
    uint8_t index = 0;
    if (misa_bits.b.mxl == 1) {
        misa_chars[index++] = '3';
        misa_chars[index++] = '2';
    } else if (misa_bits.b.mxl == 2) {
        misa_chars[index++] = '6';
        misa_chars[index++] = '4';
    } else if (misa_bits.b.mxl == 3) {
        misa_chars[index++] = '1';
        misa_chars[index++] = '2';
        misa_chars[index++] = '8';
    }
    if (misa_bits.b.i) {
        misa_chars[index++] = 'I';
    }
    if (misa_bits.b.m) {
        misa_chars[index++] = 'M';
    }
    if (misa_bits.b.a) {
        misa_chars[index++] = 'A';
    }
    if (misa_bits.b.b) {
        misa_chars[index++] = 'B';
    }
    if (misa_bits.b.c) {
        misa_chars[index++] = 'C';
    }
    if (misa_bits.b.e) {
        misa_chars[index++] = 'E';
    }
    if (misa_bits.b.f) {
        misa_chars[index++] = 'F';
    }
    if (misa_bits.b.d) {
        misa_chars[index++] = 'D';
    }
    if (misa_bits.b.q) {
        misa_chars[index++] = 'Q';
    }
    if (misa_bits.b.h) {
        misa_chars[index++] = 'H';
    }
    if (misa_bits.b.j) {
        misa_chars[index++] = 'J';
    }
    if (misa_bits.b.l) {
        misa_chars[index++] = 'L';
    }
    if (misa_bits.b.n) {
        misa_chars[index++] = 'N';
    }
    if (misa_bits.b.s) {
        misa_chars[index++] = 'S';
    }
    if (misa_bits.b.p) {
        misa_chars[index++] = 'P';
    }
    if (misa_bits.b.t) {
        misa_chars[index++] = 'T';
    }
    if (misa_bits.b.u) {
        misa_chars[index++] = 'U';
    }
    if (misa_bits.b.v) {
        misa_chars[index++] = 'V';
    }
    if (misa_bits.b.x) {
        misa_chars[index++] = 'X';
    }

    misa_chars[index++] = '\0';

    printf("MISA: RV%s\r\n", misa_chars);
}

uint32_t length = 1000;
uint64_t start_time = 0;
uint64_t end_time = 0;

// 获取时间戳函数
static inline uint64_t get_timestamp(void) { return __RV_CSR_READ(CSR_MCYCLE); }

//================= DATA FORMAT ==================
float input_data[1000] = {0};
uint16_t bf16_bn_res_array[1000] = {0};
float ref_bf16_bn_res_array[1000] = {0};
uint16_t softmax_res_array[1000] = {0};
float ref_softmax_res_array[1000] = {0};

static void fill_input_data_range(float *data, uint32_t count,
                                  float start_value, float step)
{
    for (uint32_t i = 0; i < count; i++) {
        data[i] = start_value + step * (float)i;
    }
}

// bfl16 to fp32
float bf16_to_float(uint16_t bf16)
{
    uint32_t sign = ((uint32_t)(bf16 & 0x8000)) << 16;
    uint32_t exponent = ((uint32_t)(bf16 & 0x7F80)) << 16;
    uint32_t fraction = ((uint32_t)(bf16 & 0x007F)) << 16;
    uint32_t fp32_bits = sign | exponent | fraction;
    float result;
    memcpy(&result, &fp32_bits, sizeof(float));
    return result;
}

// fp32 to bf16
uint16_t float_to_bf16(float fp32)
{
    uint32_t fp32_bits;
    memcpy(&fp32_bits, &fp32, sizeof(float));
    // 最近偶数舍入
    uint32_t lsb = (fp32_bits >> 16) & 1;
    uint32_t rounding_bias = 0x7fff + lsb;
    uint32_t rounded = fp32_bits + rounding_bias;
    return (uint16_t)(rounded >> 16);
}

// array change
void float_array2bf16_array(float *fa, uint16_t *bf16a, uint32_t array_len)
{
    for (int i = 0; i < array_len; i++) {
        bf16a[i] = float_to_bf16(fa[i]);
    }
}

void bf16_array2float_array(uint16_t *bf16a, float *fa, uint32_t array_len)
{
    for (int i = 0; i < array_len; i++) {
        fa[i] = bf16_to_float(bf16a[i]);
    }
}

//================= Normalization ==================
// NICE to compute normalization
void calculate_bn_bf16(float *op_array, uint16_t *res_array, uint32_t array_len)
{
    uint16_t bf16_array[array_len];
    uint16_t bf16_sub_res_array[array_len];
    float fp32_sub_res_array[array_len];
    float_array2bf16_array(op_array, bf16_array, array_len);

    // start time
    start_time = get_timestamp();

    //---Original Compute: Fast but Least accurate---//
    // rv_nice_bf16_setup(bf16_array, res_array);
    // rv_nice_bf16_wsetup(array_len);
    // uint16_t acc_res = rv_nice_bf16_load_acc(bf16_array);
    // //此步bf16易发生大数吞小数，导致累加不精准 uint16_t div_res =
    // rv_nice_bf16_div(acc_res); rv_nice_bf16_load_sub_store(div_res,
    // bf16_sub_res_array); uint16_t square_sum_res =
    // rv_nice_bf16_square_sum(bf16_sub_res_array);
    // //(xi-u)^2，此步bf16易发生大数吞小数，导致累加不精准 uint16_t variance =
    // rv_nice_bf16_div(square_sum_res);// (xi-u)^2/m uint16_t sd =
    // rv_nice_bf16_sqrt(variance); //  (xi-u)/m ^ 0.5
    // rv_nice_bf16_load_div_store(bf16_sub_res_array, sd); // (xi-u) / sd

    //---CPU Accumulation: Slow but Most accurate---//
    // rv_nice_bf16_setup(bf16_array, res_array);
    // rv_nice_bf16_wsetup(array_len);
    // float acc_res_fp32 = 0;
    // for (int i = 0; i < array_len; i++) {
    //	acc_res_fp32 += op_array[i];
    //}
    // uint16_t acc_res = float_to_bf16(acc_res_fp32);
    // uint16_t div_res = rv_nice_bf16_div(acc_res);
    // rv_nice_bf16_load_sub_store(div_res, bf16_sub_res_array);
    // bf16_array2float_array(bf16_sub_res_array, fp32_sub_res_array,
    // array_len); float square_sum_res_fp32 = 0; for (int i = 0; i < array_len;
    // i++) {
    //	square_sum_res_fp32 += fp32_sub_res_array[i] * fp32_sub_res_array[i];
    //}
    // uint16_t square_sum_res = float_to_bf16(square_sum_res_fp32);
    // uint16_t variance = rv_nice_bf16_div(square_sum_res);// (xi-u)^2/m
    // uint16_t sd = rv_nice_bf16_sqrt(variance); //  (xi-u)/m ^ 0.5
    // rv_nice_bf16_load_div_store(bf16_sub_res_array, sd); // (xi-u) / sd

    //---Block Accumulation: Moderate speed and accurate---//
    const uint32_t BLOCK_SIZE = 32;
    uint32_t num_blocks = (array_len + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint16_t acc_res_partial[num_blocks];
    uint16_t square_sum_res_partial[num_blocks];
    rv_nice_bf16_setup(bf16_array, res_array);
    // 第一步：分块累加
    for (uint32_t i = 0; i < num_blocks; i++) {
        uint32_t start_idx = i * BLOCK_SIZE;
        uint32_t end_idx = (start_idx + BLOCK_SIZE) < array_len
                               ? (start_idx + BLOCK_SIZE)
                               : array_len;
        uint32_t block_len = end_idx - start_idx;
        // 设置硬件处理当前块
        rv_nice_bf16_wsetup(block_len);
        // 硬件累加当前块
        acc_res_partial[i] = rv_nice_bf16_load_acc(bf16_array + start_idx);
    }
    // 合并块累加结果
    rv_nice_bf16_wsetup(num_blocks);
    uint16_t acc_res = rv_nice_bf16_load_acc(acc_res_partial);
    // 计算均值
    rv_nice_bf16_wsetup(array_len);
    uint16_t div_res = rv_nice_bf16_div(acc_res);
    // 减法操作
    rv_nice_bf16_load_sub_store(div_res, bf16_sub_res_array);
    // 第二步：分块计算平方和
    for (uint32_t i = 0; i < num_blocks; i++) {
        uint32_t start_idx = i * BLOCK_SIZE;
        uint32_t end_idx = (start_idx + BLOCK_SIZE) < array_len
                               ? (start_idx + BLOCK_SIZE)
                               : array_len;
        uint32_t block_len = end_idx - start_idx;
        // 设置硬件处理当前块
        rv_nice_bf16_wsetup(block_len);
        // 硬件计算当前块的平方和
        square_sum_res_partial[i] =
            rv_nice_bf16_square_sum(bf16_sub_res_array + start_idx);
    }
    // 合并块平方和结果
    rv_nice_bf16_wsetup(num_blocks);
    uint16_t square_sum_res = rv_nice_bf16_load_acc(square_sum_res_partial);
    // 计算方差和标准差
    rv_nice_bf16_wsetup(array_len);
    uint16_t variance = rv_nice_bf16_div(square_sum_res);
    uint16_t sd = rv_nice_bf16_sqrt(variance);
    // 除法操作
    rv_nice_bf16_load_div_store(bf16_sub_res_array, sd);

    // end time
    end_time = get_timestamp();
    printf("NICE Normalization Computing Time: %.2f cycles\n",
           (double)(end_time - start_time));
}

// CPU to compute normalization
void ref_calculate_bn_bf16(float *op_array, float *res_array,
                           uint32_t array_len)
{
    float div_res = 0;
    float sub_res_array[array_len];
    float variance = 0;
    float sd = 0;
    // start time
    start_time = get_timestamp();
    for (int i = 0; i < array_len; i++) {
        div_res += op_array[i] / array_len;
    }
    for (int i = 0; i < array_len; i++) {
        sub_res_array[i] = op_array[i] - div_res;
    }
    for (int i = 0; i < array_len; i++) {
        variance += sub_res_array[i] * sub_res_array[i] / array_len;
    }
    sd = sqrt(variance);
    for (int i = 0; i < array_len; i++) {
        ref_bf16_bn_res_array[i] = sub_res_array[i] / sd;
    }
    // end time
    end_time = get_timestamp();
    printf("CPU Normalization Computing Time: %.2f cycles\n",
           (double)(end_time - start_time));
}

// compare NICE and CPU
void cmp_bn_res(uint16_t *dut_res_array, float *ref_res_array,
                uint32_t array_len)
{
    printf("NORMALIZATION RESULT\n");
    float float_bn_res_array[array_len];
    float max_abs_diff = 0.0f;
    for (int i = 0; i < array_len; i++) {
        float_bn_res_array[i] = bf16_to_float(dut_res_array[i]);
        float abs_diff = fabsf(float_bn_res_array[i] - ref_res_array[i]);
        if (abs_diff > max_abs_diff) {
            max_abs_diff = abs_diff;
        }
    }

    uint32_t sample_count = (array_len < NICE_RESULT_SAMPLE_COUNT)
                                ? array_len
                                : NICE_RESULT_SAMPLE_COUNT;
    for (uint32_t i = 0; i < sample_count; i++) {
        printf("[NICE] %d: %f\t[CPU] %d: %f\r\n", i, float_bn_res_array[i], i,
               ref_res_array[i]);
    }
    printf("BN sample count: %lu/%lu, max abs diff: %f\r\n",
           (unsigned long)sample_count, (unsigned long)array_len, max_abs_diff);
}

//================= Softmax ==================
void calculate_softmax_bf16(float *op_array, uint16_t *res_array,
                            uint32_t array_len)
{
    uint16_t bf16_array[array_len];
    uint16_t exp_res_array[array_len];
    float exp_res_array_fp32[array_len];
    float_array2bf16_array(op_array, bf16_array, array_len);

    // start time
    start_time = get_timestamp();

    //---Original Compute: Fast but Least accurate---//
    // rv_nice_bf16_setup(bf16_array, res_array);
    // rv_nice_bf16_wsetup(array_len);
    // rv_nice_bf16_load_exp_store(bf16_array, exp_res_array);
    // uint16_t acc_exp_res = rv_nice_bf16_load_acc(exp_res_array);
    // //此步bf16易发生大数吞小数，导致累加不精准
    // rv_nice_bf16_load_div_store(exp_res_array, acc_exp_res);

    //---CPU Accumulation: Slow but Most accurate---//
    // rv_nice_bf16_setup(bf16_array, res_array);
    // rv_nice_bf16_wsetup(array_len);
    // rv_nice_bf16_load_exp_store(bf16_array, exp_res_array);
    // float acc_exp_res_fp32 = 0;
    // bf16_array2float_array(exp_res_array, exp_res_array_fp32, array_len);
    // for (int i = 0; i < array_len; i++) {
    //    acc_exp_res_fp32 += exp_res_array_fp32[i];
    //}
    // uint16_t acc_exp_res = float_to_bf16(acc_exp_res_fp32);
    // rv_nice_bf16_load_div_store(exp_res_array, acc_exp_res);

    //---Block Accumulation: Moderate speed and accurate---//
    const uint32_t BLOCK_SIZE = 512;
    uint32_t num_blocks = (array_len + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint16_t acc_exp_res_partial[num_blocks];
    rv_nice_bf16_setup(bf16_array, res_array);
    rv_nice_bf16_wsetup(array_len);
    rv_nice_bf16_load_exp_store(bf16_array, exp_res_array);
    for (uint32_t i = 0; i < num_blocks; i++) {
        uint32_t start_idx = i * BLOCK_SIZE;
        uint32_t end_idx = (start_idx + BLOCK_SIZE) < array_len
                               ? (start_idx + BLOCK_SIZE)
                               : array_len;
        uint32_t block_len = end_idx - start_idx;
        // 设置硬件处理当前块
        rv_nice_bf16_wsetup(block_len);
        // 硬件累加当前块的指数值
        acc_exp_res_partial[i] =
            rv_nice_bf16_load_acc(exp_res_array + start_idx);
    }
    // 合并结果
    rv_nice_bf16_wsetup(num_blocks);
    uint16_t acc_exp_res = rv_nice_bf16_load_acc(acc_exp_res_partial);
    rv_nice_bf16_wsetup(array_len);
    rv_nice_bf16_load_div_store(exp_res_array, acc_exp_res);

    // end time
    end_time = get_timestamp();
    printf("NICE Softmax Computing Time: %.2f cycles\n",
           (double)(end_time - start_time));
}

void ref_calculate_softmax(float *op_array, float *res_array,
                           uint32_t array_len)
{
    float ref_exp_res_array[array_len];
    // start time
    start_time = get_timestamp();
    for (int i = 0; i < array_len; i++) {
        ref_exp_res_array[i] = exp(op_array[i]);
    }
    float ref_acc_exp_res = 0;
    for (int i = 0; i < array_len; i++) {
        ref_acc_exp_res += ref_exp_res_array[i];
    }
    for (int i = 0; i < array_len; i++) {
        res_array[i] = ref_exp_res_array[i] / ref_acc_exp_res;
    }
    // end time
    end_time = get_timestamp();
    printf("CPU Softmax Computing Time: %.2f cycles\n",
           (double)(end_time - start_time));
}

void cmp_softmax_res(uint16_t *dut_res_array, float *ref_res_array,
                     uint32_t array_len)
{
    printf("SOFTMAX RESULT\n");
    float float_softmax_res_array[array_len];
    float max_abs_diff = 0.0f;
    for (int i = 0; i < array_len; i++) {
        float_softmax_res_array[i] = bf16_to_float(dut_res_array[i]);
        float abs_diff = fabsf(float_softmax_res_array[i] - ref_res_array[i]);
        if (abs_diff > max_abs_diff) {
            max_abs_diff = abs_diff;
        }
    }
    uint32_t sample_count = (array_len < NICE_RESULT_SAMPLE_COUNT)
                                ? array_len
                                : NICE_RESULT_SAMPLE_COUNT;
    for (uint32_t i = 0; i < sample_count; i++) {
        printf("[NICE] %d: %f\t[CPU] %d: %f\r\n", i, float_softmax_res_array[i],
               i, ref_res_array[i]);
    }
    printf("Softmax sample count: %lu/%lu, max abs diff: %f\r\n",
           (unsigned long)sample_count, (unsigned long)array_len, max_abs_diff);
}

int main(void)
{
    printf("Check NICE interface...\n");
    print_misa();

    fill_input_data_range(input_data, length, 1.0f, 1.0f);

    // NICE to compute normalization
    RV_NICE_ENABLE();
    calculate_bn_bf16(input_data, bf16_bn_res_array, length);

    // CPU to compute normalization
    ref_calculate_bn_bf16(input_data, ref_bf16_bn_res_array, length);

    // result comparison
    cmp_bn_res(bf16_bn_res_array, ref_bf16_bn_res_array, length);

    printf(" \n");

    fill_input_data_range(input_data, length, -999.0f, 1.0f);

    // NICE to compute softmax
    calculate_softmax_bf16(input_data, softmax_res_array, length);

    // CPU to compute softmax
    ref_calculate_softmax(input_data, ref_softmax_res_array, length);

    // result comparison
    cmp_softmax_res(softmax_res_array, ref_softmax_res_array, length);

    printf("Done\n");

    RV_NICE_DISABLE();
    return 0;
}
