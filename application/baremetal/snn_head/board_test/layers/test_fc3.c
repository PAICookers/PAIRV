/*
 * fc3 单层上板测试（独立程序）。
 *   输入: golden_fc2_out[8][1536]（上一层 golden 输出，隔离测）
 *
 * 只负责喂输入、跑真实层函数；不做任何数值对比。中间/最终结果由层内 SNN_HEAD_DUMP
 * 通过串口打印（需 make DUMP=1），PC 端读出后自行与 golden 逐元素对比。
 * 需要 fixtures/fc3/compile_artifacts.bin；到位后 Makefile 会自动纳入 fc3 artifact。
 */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "snn_head_golden.h"
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
    if (!snn_head_run_fc3(action)) {
        printf("fc3     : RUN FAILED\r\n");
        return 1;
    }
    printf("fc3     : DONE\r\n");
    return 0;
}
