/*
 * fc1_lif 单层上板测试（独立程序）。
 *   输入: golden_input[8][768]
 *
 * 只负责喂输入、跑真实层函数；不做任何数值对比。所有中间/最终结果
 * （②LN / ③量化int8 / ④PAICore原始输出 / ⑤本层输出）由层内 SNN_HEAD_DUMP
 * 通过串口打印（需 make DUMP=1），PC 端读出后自行与 golden 逐元素对比。
 */
#include <stdio.h>

#include "debug.h"
#include "snn_head_golden.h"
#include "snn_head_internal.h"

int main(void)
{
    rv_debug_set_level(RV_DEBUG_ERROR);
    printf("== snn_head layer test [fc1_lif]  golden_ready=%d ==\r\n",
           snn_head_golden_ready);

    if (!snn_head_run_fc1_lif(snn_head_golden_input)) {
        printf("fc1_lif : RUN FAILED\r\n");
        return 1;
    }
    printf("fc1_lif : DONE\r\n");
    return 0;
}
