#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "nuclei_sdk_soc.h"
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "insn.h"

void print_misa(void)
{
    CSR_MISA_Type misa_bits = (CSR_MISA_Type)__RV_CSR_READ(CSR_MISA);
    static char misa_chars[30];
    uint8_t index = 0;
    if (misa_bits.b.mxl == 1)
    {
        misa_chars[index++] = '3';
        misa_chars[index++] = '2';
    }
    else if (misa_bits.b.mxl == 2)
    {
        misa_chars[index++] = '6';
        misa_chars[index++] = '4';
    }
    else if (misa_bits.b.mxl == 3)
    {
        misa_chars[index++] = '1';
        misa_chars[index++] = '2';
        misa_chars[index++] = '8';
    }
    if (misa_bits.b.i)
    {
        misa_chars[index++] = 'I';
    }
    if (misa_bits.b.m)
    {
        misa_chars[index++] = 'M';
    }
    if (misa_bits.b.a)
    {
        misa_chars[index++] = 'A';
    }
    if (misa_bits.b.b)
    {
        misa_chars[index++] = 'B';
    }
    if (misa_bits.b.c)
    {
        misa_chars[index++] = 'C';
    }
    if (misa_bits.b.e)
    {
        misa_chars[index++] = 'E';
    }
    if (misa_bits.b.f)
    {
        misa_chars[index++] = 'F';
    }
    if (misa_bits.b.d)
    {
        misa_chars[index++] = 'D';
    }
    if (misa_bits.b.q)
    {
        misa_chars[index++] = 'Q';
    }
    if (misa_bits.b.h)
    {
        misa_chars[index++] = 'H';
    }
    if (misa_bits.b.j)
    {
        misa_chars[index++] = 'J';
    }
    if (misa_bits.b.l)
    {
        misa_chars[index++] = 'L';
    }
    if (misa_bits.b.n)
    {
        misa_chars[index++] = 'N';
    }
    if (misa_bits.b.s)
    {
        misa_chars[index++] = 'S';
    }
    if (misa_bits.b.p)
    {
        misa_chars[index++] = 'P';
    }
    if (misa_bits.b.t)
    {
        misa_chars[index++] = 'T';
    }
    if (misa_bits.b.u)
    {
        misa_chars[index++] = 'U';
    }
    if (misa_bits.b.v)
    {
        misa_chars[index++] = 'V';
    }
    if (misa_bits.b.x)
    {
        misa_chars[index++] = 'X';
    }

    misa_chars[index++] = '\0';

    printf("MISA: RV%s\r\n", misa_chars);
}

#define TEST_COUNT 10000

uint32_t length = 1000;
uint64_t start_time = 0;
uint64_t end_time = 0;

// 获取时间戳函数
static inline uint64_t get_timestamp(void)
{
    return __RV_CSR_READ(CSR_MCYCLE);
}

//================= DATA FORMAT ==================
float input_data[1000] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
    71, 72, 73, 74, 75, 76, 77, 78, 79, 80,
    81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
    91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
    101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
    111, 112, 113, 114, 115, 116, 117, 118, 119, 120,
    121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
    131, 132, 133, 134, 135, 136, 137, 138, 139, 140,
    141, 142, 143, 144, 145, 146, 147, 148, 149, 150,
    151, 152, 153, 154, 155, 156, 157, 158, 159, 160,
    161, 162, 163, 164, 165, 166, 167, 168, 169, 170,
    171, 172, 173, 174, 175, 176, 177, 178, 179, 180,
    181, 182, 183, 184, 185, 186, 187, 188, 189, 190,
    191, 192, 193, 194, 195, 196, 197, 198, 199, 200,
    201, 202, 203, 204, 205, 206, 207, 208, 209, 210,
    211, 212, 213, 214, 215, 216, 217, 218, 219, 220,
    221, 222, 223, 224, 225, 226, 227, 228, 229, 230,
    231, 232, 233, 234, 235, 236, 237, 238, 239, 240,
    241, 242, 243, 244, 245, 246, 247, 248, 249, 250,
    251, 252, 253, 254, 255, 256, 257, 258, 259, 260,
    261, 262, 263, 264, 265, 266, 267, 268, 269, 270,
    271, 272, 273, 274, 275, 276, 277, 278, 279, 280,
    281, 282, 283, 284, 285, 286, 287, 288, 289, 290,
    291, 292, 293, 294, 295, 296, 297, 298, 299, 300,
    301, 302, 303, 304, 305, 306, 307, 308, 309, 310,
    311, 312, 313, 314, 315, 316, 317, 318, 319, 320,
    321, 322, 323, 324, 325, 326, 327, 328, 329, 330,
    331, 332, 333, 334, 335, 336, 337, 338, 339, 340,
    341, 342, 343, 344, 345, 346, 347, 348, 349, 350,
    351, 352, 353, 354, 355, 356, 357, 358, 359, 360,
    361, 362, 363, 364, 365, 366, 367, 368, 369, 370,
    371, 372, 373, 374, 375, 376, 377, 378, 379, 380,
    381, 382, 383, 384, 385, 386, 387, 388, 389, 390,
    391, 392, 393, 394, 395, 396, 397, 398, 399, 400,
    401, 402, 403, 404, 405, 406, 407, 408, 409, 410,
    411, 412, 413, 414, 415, 416, 417, 418, 419, 420,
    421, 422, 423, 424, 425, 426, 427, 428, 429, 430,
    431, 432, 433, 434, 435, 436, 437, 438, 439, 440,
    441, 442, 443, 444, 445, 446, 447, 448, 449, 450,
    451, 452, 453, 454, 455, 456, 457, 458, 459, 460,
    461, 462, 463, 464, 465, 466, 467, 468, 469, 470,
    471, 472, 473, 474, 475, 476, 477, 478, 479, 480,
    481, 482, 483, 484, 485, 486, 487, 488, 489, 490,
    491, 492, 493, 494, 495, 496, 497, 498, 499, 500,
    501, 502, 503, 504, 505, 506, 507, 508, 509, 510,
    511, 512, 513, 514, 515, 516, 517, 518, 519, 520,
    521, 522, 523, 524, 525, 526, 527, 528, 529, 530,
    531, 532, 533, 534, 535, 536, 537, 538, 539, 540,
    541, 542, 543, 544, 545, 546, 547, 548, 549, 550,
    551, 552, 553, 554, 555, 556, 557, 558, 559, 560,
    561, 562, 563, 564, 565, 566, 567, 568, 569, 570,
    571, 572, 573, 574, 575, 576, 577, 578, 579, 580,
    581, 582, 583, 584, 585, 586, 587, 588, 589, 590,
    591, 592, 593, 594, 595, 596, 597, 598, 599, 600,
    601, 602, 603, 604, 605, 606, 607, 608, 609, 610,
    611, 612, 613, 614, 615, 616, 617, 618, 619, 620,
    621, 622, 623, 624, 625, 626, 627, 628, 629, 630,
    631, 632, 633, 634, 635, 636, 637, 638, 639, 640,
    641, 642, 643, 644, 645, 646, 647, 648, 649, 650,
    651, 652, 653, 654, 655, 656, 657, 658, 659, 660,
    661, 662, 663, 664, 665, 666, 667, 668, 669, 670,
    671, 672, 673, 674, 675, 676, 677, 678, 679, 680,
    681, 682, 683, 684, 685, 686, 687, 688, 689, 690,
    691, 692, 693, 694, 695, 696, 697, 698, 699, 700,
    701, 702, 703, 704, 705, 706, 707, 708, 709, 710,
    711, 712, 713, 714, 715, 716, 717, 718, 719, 720,
    721, 722, 723, 724, 725, 726, 727, 728, 729, 730,
    731, 732, 733, 734, 735, 736, 737, 738, 739, 740,
    741, 742, 743, 744, 745, 746, 747, 748, 749, 750,
    751, 752, 753, 754, 755, 756, 757, 758, 759, 760,
    761, 762, 763, 764, 765, 766, 767, 768, 769, 770,
    771, 772, 773, 774, 775, 776, 777, 778, 779, 780,
    781, 782, 783, 784, 785, 786, 787, 788, 789, 790,
    791, 792, 793, 794, 795, 796, 797, 798, 799, 800,
    801, 802, 803, 804, 805, 806, 807, 808, 809, 810,
    811, 812, 813, 814, 815, 816, 817, 818, 819, 820,
    821, 822, 823, 824, 825, 826, 827, 828, 829, 830,
    831, 832, 833, 834, 835, 836, 837, 838, 839, 840,
    841, 842, 843, 844, 845, 846, 847, 848, 849, 850,
    851, 852, 853, 854, 855, 856, 857, 858, 859, 860,
    861, 862, 863, 864, 865, 866, 867, 868, 869, 870,
    871, 872, 873, 874, 875, 876, 877, 878, 879, 880,
    881, 882, 883, 884, 885, 886, 887, 888, 889, 890,
    891, 892, 893, 894, 895, 896, 897, 898, 899, 900,
    901, 902, 903, 904, 905, 906, 907, 908, 909, 910,
    911, 912, 913, 914, 915, 916, 917, 918, 919, 920,
    921, 922, 923, 924, 925, 926, 927, 928, 929, 930,
    931, 932, 933, 934, 935, 936, 937, 938, 939, 940,
    941, 942, 943, 944, 945, 946, 947, 948, 949, 950,
    951, 952, 953, 954, 955, 956, 957, 958, 959, 960,
    961, 962, 963, 964, 965, 966, 967, 968, 969, 970,
    971, 972, 973, 974, 975, 976, 977, 978, 979, 980,
    981, 982, 983, 984, 985, 986, 987, 988, 989, 990,
    991, 992, 993, 994, 995, 996, 997, 998, 999, 1000};
uint16_t bf16_bn_res_array[1000] = {0};
float ref_bf16_bn_res_array[1000] = {0};
uint16_t softmax_res_array[1000] = {0};
float ref_softmax_res_array[1000] = {0};

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
    for (int i = 0; i < array_len; i++)
    {
        bf16a[i] = float_to_bf16(fa[i]);
    }
}

void bf16_array2float_array(uint16_t *bf16a, float *fa, uint32_t array_len)
{
    for (int i = 0; i < array_len; i++)
    {
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
    // custom_bf16_setup(bf16_array, res_array);
    // custom_bf16_wsetup(array_len);
    // uint16_t acc_res = custom_bf16_load_acc(bf16_array); //此步bf16易发生大数吞小数，导致累加不精准
    // uint16_t div_res = custom_bf16_div(acc_res);
    // custom_bf16_load_sub_store(div_res, bf16_sub_res_array);
    // uint16_t square_sum_res = custom_bf16_square_sum(bf16_sub_res_array); //(xi-u)^2，此步bf16易发生大数吞小数，导致累加不精准
    // uint16_t variance = custom_bf16_div(square_sum_res);// (xi-u)^2/m
    // uint16_t sd = custom_bf16_sqrt(variance); //  (xi-u)/m ^ 0.5
    // custom_bf16_load_div_store(bf16_sub_res_array, sd); // (xi-u) / sd

    //---CPU Accumulation: Slow but Most accurate---//
    // custom_bf16_setup(bf16_array, res_array);
    // custom_bf16_wsetup(array_len);
    // float acc_res_fp32 = 0;
    // for (int i = 0; i < array_len; i++) {
    //	acc_res_fp32 += op_array[i];
    //}
    // uint16_t acc_res = float_to_bf16(acc_res_fp32);
    // uint16_t div_res = custom_bf16_div(acc_res);
    // custom_bf16_load_sub_store(div_res, bf16_sub_res_array);
    // bf16_array2float_array(bf16_sub_res_array, fp32_sub_res_array, array_len);
    // float square_sum_res_fp32 = 0;
    // for (int i = 0; i < array_len; i++) {
    //	square_sum_res_fp32 += fp32_sub_res_array[i] * fp32_sub_res_array[i];
    //}
    // uint16_t square_sum_res = float_to_bf16(square_sum_res_fp32);
    // uint16_t variance = custom_bf16_div(square_sum_res);// (xi-u)^2/m
    // uint16_t sd = custom_bf16_sqrt(variance); //  (xi-u)/m ^ 0.5
    // custom_bf16_load_div_store(bf16_sub_res_array, sd); // (xi-u) / sd

    //---Block Accumulation: Moderate speed and accurate---//
    const uint32_t BLOCK_SIZE = 32;
    uint32_t num_blocks = (array_len + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint16_t acc_res_partial[num_blocks];
    uint16_t square_sum_res_partial[num_blocks];
    custom_bf16_setup(bf16_array, res_array);
    // 第一步：分块累加
    for (uint32_t i = 0; i < num_blocks; i++)
    {
        uint32_t start_idx = i * BLOCK_SIZE;
        uint32_t end_idx = (start_idx + BLOCK_SIZE) < array_len ? (start_idx + BLOCK_SIZE) : array_len;
        uint32_t block_len = end_idx - start_idx;
        // 设置硬件处理当前块
        custom_bf16_wsetup(block_len);
        // 硬件累加当前块
        acc_res_partial[i] = custom_bf16_load_acc(bf16_array + start_idx);
    }
    // 合并块累加结果
    custom_bf16_wsetup(num_blocks);
    uint16_t acc_res = custom_bf16_load_acc(acc_res_partial);
    // 计算均值
    custom_bf16_wsetup(array_len);
    uint16_t div_res = custom_bf16_div(acc_res);
    // 减法操作
    custom_bf16_load_sub_store(div_res, bf16_sub_res_array);
    // 第二步：分块计算平方和
    for (uint32_t i = 0; i < num_blocks; i++)
    {
        uint32_t start_idx = i * BLOCK_SIZE;
        uint32_t end_idx = (start_idx + BLOCK_SIZE) < array_len ? (start_idx + BLOCK_SIZE) : array_len;
        uint32_t block_len = end_idx - start_idx;
        // 设置硬件处理当前块
        custom_bf16_wsetup(block_len);
        // 硬件计算当前块的平方和
        square_sum_res_partial[i] = custom_bf16_square_sum(bf16_sub_res_array + start_idx);
    }
    // 合并块平方和结果
    custom_bf16_wsetup(num_blocks);
    uint16_t square_sum_res = custom_bf16_load_acc(square_sum_res_partial);
    // 计算方差和标准差
    custom_bf16_wsetup(array_len);
    uint16_t variance = custom_bf16_div(square_sum_res);
    uint16_t sd = custom_bf16_sqrt(variance);
    // 除法操作
    custom_bf16_load_div_store(bf16_sub_res_array, sd);

    // end time
    end_time = get_timestamp();
    printf("NICE Normalization Computing Time: %.2f cycles\n", (double)(end_time - start_time));
}

// CPU to compute normalization
void ref_calculate_bn_bf16(float *op_array, float *res_array, uint32_t array_len)
{
    float div_res = 0;
    float sub_res_array[array_len];
    float variance = 0;
    float sd = 0;
    // start time
    start_time = get_timestamp();
    for (int i = 0; i < array_len; i++)
    {
        div_res += op_array[i] / array_len;
    }
    for (int i = 0; i < array_len; i++)
    {
        sub_res_array[i] = op_array[i] - div_res;
    }
    for (int i = 0; i < array_len; i++)
    {
        variance += sub_res_array[i] * sub_res_array[i] / array_len;
    }
    sd = sqrt(variance);
    for (int i = 0; i < array_len; i++)
    {
        ref_bf16_bn_res_array[i] = sub_res_array[i] / sd;
    }
    // end time
    end_time = get_timestamp();
    printf("CPU Normalization Computing Time: %.2f cycles\n", (double)(end_time - start_time));
}

// compare NICE and CPU
void cmp_bn_res(uint16_t *dut_res_array, float *ref_res_array, uint32_t array_len)
{
    printf("NORMALIZATION RESULT\n");
    float float_bn_res_array[array_len];
    for (int i = 0; i < array_len; i++)
    {
        float_bn_res_array[i] = bf16_to_float(dut_res_array[i]);
    }

    for (int i = 0; i < array_len; i++)
    {
        printf("[NICE] %d: %f\t[CPU] %d: %f\r\n", i, float_bn_res_array[i], i, ref_res_array[i]);
    }
}

//================= Softmax ==================
void calculate_softmax_bf16(float *op_array, uint16_t *res_array, uint32_t array_len)
{
    uint16_t bf16_array[array_len];
    uint16_t exp_res_array[array_len];
    float exp_res_array_fp32[array_len];
    float_array2bf16_array(op_array, bf16_array, array_len);

    // start time
    start_time = get_timestamp();

    //---Original Compute: Fast but Least accurate---//
    // custom_bf16_setup(bf16_array, res_array);
    // custom_bf16_wsetup(array_len);
    // custom_bf16_load_exp_store(bf16_array, exp_res_array);
    // uint16_t acc_exp_res = custom_bf16_load_acc(exp_res_array); //此步bf16易发生大数吞小数，导致累加不精准
    // custom_bf16_load_div_store(exp_res_array, acc_exp_res);

    //---CPU Accumulation: Slow but Most accurate---//
    // custom_bf16_setup(bf16_array, res_array);
    // custom_bf16_wsetup(array_len);
    // custom_bf16_load_exp_store(bf16_array, exp_res_array);
    // float acc_exp_res_fp32 = 0;
    // bf16_array2float_array(exp_res_array, exp_res_array_fp32, array_len);
    // for (int i = 0; i < array_len; i++) {
    //    acc_exp_res_fp32 += exp_res_array_fp32[i];
    //}
    // uint16_t acc_exp_res = float_to_bf16(acc_exp_res_fp32);
    // custom_bf16_load_div_store(exp_res_array, acc_exp_res);

    //---Block Accumulation: Moderate speed and accurate---//
    const uint32_t BLOCK_SIZE = 512;
    uint32_t num_blocks = (array_len + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint16_t acc_exp_res_partial[num_blocks];
    custom_bf16_setup(bf16_array, res_array);
    custom_bf16_wsetup(array_len);
    custom_bf16_load_exp_store(bf16_array, exp_res_array);
    for (uint32_t i = 0; i < num_blocks; i++)
    {
        uint32_t start_idx = i * BLOCK_SIZE;
        uint32_t end_idx = (start_idx + BLOCK_SIZE) < array_len ? (start_idx + BLOCK_SIZE) : array_len;
        uint32_t block_len = end_idx - start_idx;
        // 设置硬件处理当前块
        custom_bf16_wsetup(block_len);
        // 硬件累加当前块的指数值
        acc_exp_res_partial[i] = custom_bf16_load_acc(exp_res_array + start_idx);
    }
    // 合并结果
    custom_bf16_wsetup(num_blocks);
    uint16_t acc_exp_res = custom_bf16_load_acc(acc_exp_res_partial);
    custom_bf16_wsetup(array_len);
    custom_bf16_load_div_store(exp_res_array, acc_exp_res);

    // end time
    end_time = get_timestamp();
    printf("NICE Softmax Computing Time: %.2f cycles\n", (double)(end_time - start_time));
}

void ref_calculate_softmax(float *op_array, float *res_array, uint32_t array_len)
{
    float ref_exp_res_array[array_len];
    // start time
    start_time = get_timestamp();
    for (int i = 0; i < array_len; i++)
    {
        ref_exp_res_array[i] = exp(op_array[i]);
    }
    float ref_acc_exp_res = 0;
    for (int i = 0; i < array_len; i++)
    {
        ref_acc_exp_res += ref_exp_res_array[i];
    }
    for (int i = 0; i < array_len; i++)
    {
        res_array[i] = ref_exp_res_array[i] / ref_acc_exp_res;
    }
    // end time
    end_time = get_timestamp();
    printf("CPU Softmax Computing Time: %.2f cycles\n", (double)(end_time - start_time));
}

void cmp_softmax_res(uint16_t *dut_res_array, float *ref_res_array, uint32_t array_len)
{
    printf("SOFTMAX RESULT\n");
    float float_softmax_res_array[array_len];
    for (int i = 0; i < array_len; i++)
    {
        float_softmax_res_array[i] = bf16_to_float(dut_res_array[i]);
    }
    for (int i = 0; i < array_len; i++)
    {
        printf("[NICE] %d: %f\t[CPU] %d: %f\r\n", i, float_softmax_res_array[i], i, ref_res_array[i]);
    }
}

int main(void)
{
    uint32_t rval, seed;
    unsigned long hartid, clusterid;
    rv_csr_t misa;

    // get hart id of current cluster
    hartid = __get_hart_id();
    clusterid = __get_cluster_id();
    misa = __RV_CSR_READ(CSR_MISA);

    __RV_CSR_SET(CSR_MSTATUS, MSTATUS_XS);

    printf("Check NICE interface...\n");
    print_misa();

    printf(" \n");

    for (int i = 0; i < length; i++)
    {
        input_data[i] = input_data[i];
    }

    // NICE to compute normalization
    calculate_bn_bf16(input_data, bf16_bn_res_array, length);

    // CPU to compute normalization
    ref_calculate_bn_bf16(input_data, ref_bf16_bn_res_array, length);

    // result comparison
    cmp_bn_res(bf16_bn_res_array, ref_bf16_bn_res_array, length);

    printf(" \n");

    for (int i = 0; i < length; i++)
    {
        input_data[i] = input_data[i] - 1000;
    }

    // NICE to compute softmax
    calculate_softmax_bf16(input_data, softmax_res_array, length);

    // CPU to compute softmax
    ref_calculate_softmax(input_data, ref_softmax_res_array, length);

    // result comparison
    cmp_softmax_res(softmax_res_array, ref_softmax_res_array, length);

    return 0;
}
