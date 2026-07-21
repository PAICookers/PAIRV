/*
 * 端到端整链路上板测试（独立程序）：make LAYER=chain
 *   输入: golden_input[8][768]
 *
 * 与单层隔离测（test_fc1_lif 等）的本质区别：隔离测给每层喂 golden 输入，掩盖了
 * 层间真实数据通路；本测试跑真实 snn_head_run_chunk()，让五层
 * （fc1_lif -> block0 -> block1 -> fc2 -> fc3）用同一块 tensor_workspace 接力，
 * 完整走过 LN / 量化 / stride 扩排 / spike 拓宽 / 编解码 的真实搬运路径，验证整条链
 * 在真芯片上跑通且数值一致。
 *
 * 前置条件：fixtures 完整（含 fc3），且各层 artifact 为 timesteps=1 单步图，否则
 * snn_head_run_chunk 会在某层 validate 处返回 false（打印 RUN FAILED）。
 *
 * 输出：无论是否 DUMP，都以机器可解析格式打印最终 action[8][7]（与 dump sink 的
 * F32 段格式一致：`<idx> 0x<IEEE754位> <round(v*1e6)>`），供 PC 端对 golden_fc3_out
 * 逐元素精确对比（不做板上判定，与其它单层测试一致）。
 * make DUMP=1 时各层还会打印 ②LN/③量化/④芯片原始/⑤输出，便于定位差异在哪一层。
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "snn_head.h"
#include "snn_head_golden.h"

int main(void)
{
    rv_debug_set_level(RV_DEBUG_ERROR);
    printf("== snn_head chain test [end-to-end]  golden_ready=%d ==\r\n",
           snn_head_golden_ready);

    float action[SNN_HEAD_GOLDEN_ACTION_ELEMS] = {0};

    /* 真实整链路：五层用同一 tensor_workspace 接力，最终写出 action[8][7]。 */
    if (!snn_head_run_chunk(snn_head_golden_input, action)) {
        printf("chain   : RUN FAILED\r\n");
        return 1;
    }

    /* 机器可解析地打印最终 action（56 个 float，格式与 snn_head_dump_sink 的 F32 一致）。 */
    printf(">>> DUMP chain.action count=%u dtype=%d\r\n",
           (unsigned)SNN_HEAD_GOLDEN_ACTION_ELEMS, 0);
    for (uint32_t i = 0U; i < SNN_HEAD_GOLDEN_ACTION_ELEMS; ++i) {
        const float v = action[i];
        uint32_t bits = 0U;
        memcpy(&bits, &v, sizeof(bits));
        const long micro =
            (long)((double)v * 1000000.0 + ((v >= 0.0f) ? 0.5 : -0.5));
        printf("%u 0x%08lx %ld\r\n", (unsigned)i, (unsigned long)bits, micro);
    }
    printf("<<< END chain.action\r\n");

    /* 不做板上判定：与其它单层测试一致，只跑通 + 打印，权威对比在 PC 端逐元素做。 */
    printf("chain   : DONE (compare chain.action vs golden_fc3_out on PC)\r\n");
    return 0;
}
