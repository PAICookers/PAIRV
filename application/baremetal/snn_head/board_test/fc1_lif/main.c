/*
 * fc1_lif 单层上板测试（独立程序）。
 *   输入: golden_input[8][768]
 *
 * 以固定 golden 输入运行真实层函数，并在板上精确比较本层 spike 输出。
 * make DUMP=1 可额外打印层内中间张量以定位失败。
 */
#include <stdio.h>

#include "../profile_report.h"
#include "debug.h"
#include "snn_head_golden.h"
#include "snn_head_golden_compare.h"
#include "snn_head_internal.h"

int main(void)
{
    rv_debug_set_level(RV_DEBUG_ERROR);
    printf("== snn_head layer test [fc1_lif]  golden_ready=%d ==\r\n",
           snn_head_golden_ready);

    printf("fc1_lif : CONFIG/INIT deploy begin\r\n");
    SNN_HEAD_PROFILE_RESET();
    SNN_HEAD_PROFILE_LAYER_BEGIN(SNN_HEAD_LAYER_FC1_LIF);
    const bool run_ok = snn_head_run_fc1_lif(snn_head_golden_input);
    SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_FC1_LIF);
    if (!run_ok) {
        printf("fc1_lif : RUN FAILED\r\n");
        return 1;
    }
    printf("fc1_lif : CONFIG/INIT/WORK complete\r\n");
    const int result = snn_head_golden_compare_exact_f32(
        "fc1_lif", (const float *)tensor_workspace, snn_head_golden_fc1_out,
        SNN_HEAD_GOLDEN_HIDDEN_ELEMS);
    if (result == 0) {
        snn_head_board_profile_print();
    }
    return result;
}
