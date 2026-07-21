/*
 * block1 (LN->fc->LIF) 单层上板测试（独立程序）。
 *   输入: golden_block0_out[8][1536]（上一层 golden 输出，隔离测）
 *
 * 只负责喂输入、跑真实层函数；不做任何数值对比。中间/最终结果由层内 SNN_HEAD_DUMP
 * 通过串口打印（需 make DUMP=1），PC 端读出后自行与 golden 逐元素对比。
 */
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "snn_head_golden.h"
#include "snn_head_internal.h"

int main(void)
{
    rv_debug_set_level(RV_DEBUG_ERROR);
    printf("== snn_head layer test [block1]  golden_ready=%d ==\r\n",
           snn_head_golden_ready);

    /* 隔离输入：把上一层的 golden 输出预载进 workspace 作本层输入。 */
    memcpy(tensor_workspace, snn_head_golden_block0_out,
           (size_t)SNN_HEAD_GOLDEN_HIDDEN_ELEMS * sizeof(float));

    if (!snn_head_run_block_lif(snn_head_block1_ln_weight,
                                snn_head_block1_ln_bias,
                                snn_head_block1_activation_scale,
                                snn_head_block1_lif_artifact_start,
                                snn_head_block1_lif_artifact_size,
                                &SNN_HEAD_BLOCK_LIF_CONTRACT)) {
        printf("block1  : RUN FAILED\r\n");
        return 1;
    }
    printf("block1  : DONE\r\n");
    return 0;
}
