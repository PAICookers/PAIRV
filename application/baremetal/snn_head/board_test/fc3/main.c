/*
 * fc3 单层上板测试（独立程序）。
 *   输入: golden_fc2_out[8][1536]（上一层 golden 输出，隔离测）
 *
 * 以固定 golden 输入运行真实层函数，并在板上比较最终 action。
 * 使用 assets/fc3/compile_artifacts.bin；Makefile 会自动纳入 fc3 artifact。
 * artifact。
 */
#include <stdio.h>
#include <string.h>

#include "../profile_report.h"
#include "debug.h"
#include "snn_head_golden.h"
#include "snn_head_golden_compare.h"
#include "snn_head_internal.h"

int main(void)
{
    rv_debug_set_level(RV_DEBUG_ERROR);
    printf("== snn_head layer test [fc3]  golden_ready=%d ==\r\n",
           snn_head_golden_ready);

    /* 隔离输入：把上一层的 golden 输出预载进 workspace 作本层输入。 */
    memcpy(tensor_workspace, snn_head_golden_fc2_out,
           (size_t)SNN_HEAD_GOLDEN_HIDDEN_ELEMS * sizeof(float));

    float action[SNN_HEAD_GOLDEN_ACTION_ELEMS] = {0};
    SNN_HEAD_PROFILE_RESET();
    SNN_HEAD_PROFILE_LAYER_BEGIN(SNN_HEAD_LAYER_FC3);
    const bool run_ok = snn_head_run_fc3(action);
    SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_FC3);
    if (!run_ok) {
        printf("fc3     : RUN FAILED\r\n");
        return 1;
    }
    const int result = snn_head_golden_compare_close_f32(
        "fc3", action, snn_head_golden_fc3_out, SNN_HEAD_GOLDEN_ACTION_ELEMS);
    if (result == 0) {
        snn_head_board_profile_print();
    }
    return result;
}
