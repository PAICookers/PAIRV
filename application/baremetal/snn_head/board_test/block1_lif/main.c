/*
 * block1 (LN->fc->LIF) 单层上板测试（独立程序）。
 *   输入: golden_block0_out[8][1536]（上一层 golden 输出，隔离测）
 *
 * 以固定 golden 输入运行真实层函数，并在板上精确比较本层 spike 输出。
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
    rv_debug_set_level(RV_DEBUG_INFO);
    printf("== snn_head layer test [block1]  golden_ready=%d ==\r\n",
           snn_head_golden_ready);

    /* 隔离输入：把上一层的 golden 输出预载进 workspace 作本层输入。 */
    memcpy(tensor_workspace, snn_head_golden_block0_out,
           (size_t)SNN_HEAD_GOLDEN_HIDDEN_ELEMS * sizeof(float));

    printf("block1  : PAICORE INIT enabled\r\n");
    SNN_HEAD_PROFILE_RESET();
    SNN_HEAD_PROFILE_LAYER_BEGIN(SNN_HEAD_LAYER_BLOCK1);
    const bool run_ok = snn_head_run_block_lif(
        snn_head_block1_ln_weight, snn_head_block1_ln_bias,
        SNN_HEAD_LAYER_BLOCK1, snn_head_block1_activation_scale,
        snn_head_block1_lif_artifact_start, snn_head_block1_lif_artifact_size,
        &SNN_HEAD_BLOCK_LIF_CONTRACT);
    SNN_HEAD_PROFILE_LAYER_END(SNN_HEAD_LAYER_BLOCK1);
    if (!run_ok) {
        printf("block1  : RUN FAILED\r\n");
        return 1;
    }
    const int result = snn_head_golden_compare_all_exact_f32(
        "block1", (const float *)tensor_workspace, snn_head_golden_block1_out,
        SNN_HEAD_GOLDEN_HIDDEN_ELEMS);
    if (result == 0) {
        snn_head_board_profile_print();
    }
    return result;
}
