#ifndef SNN_HEAD_GOLDEN_H
#define SNN_HEAD_GOLDEN_H

/*
 * 逐层上板测试的 golden 数据接口。
 *
 * 这些数组由 CPU INT8 边界算子和 PAIBox lowering 后的 PAICORE LIF 参数，对一个
 * 固定输入前向后导出。LIF beta 与阈值采用 artifact 的部署语义，而非 QAT runtime
 * 的精确浮点 beta；数组表示与 C workspace 完全一致：
 *
 *   snn_head_golden_input      : fc1_lif 的输入   float32[8][768]（chunk 输入）
 *   snn_head_golden_fc1_out    : lif_in  的 spike float32[8][1536]（0/1），=
 * block0 输入 snn_head_golden_block0_out : block0  的 spike
 * float32[8][1536]（0/1），= block1 输入 snn_head_golden_block1_out : block1 的
 * spike float32[8][1536]（0/1），= fc2 输入 snn_head_golden_fc2_out    : li_out
 * 膜电位反量化 float32[8][1536]，= fc3 输入 （= li_out.mem_int32 * fc2
 * per-channel output_scale） snn_head_golden_fc3_out    : 最终动作
 * float32[8][7]（= fc3 acc_int32 * output_scale）
 *
 * 每层的 golden 输出正好是下一层的 golden 输入，所以同一套数据既支持“隔离测”
 * （给每层喂 golden 输入，坏了立刻定位是哪层），也能串成端到端参考。
 *
 * 未生成真实 golden 前，snn_head_golden.c 是 ready=0 的占位（全 0）；运行
 * gen_snn_head_golden.py 会用真实数据覆盖它并置 ready=1。
 */

#include <stdint.h>

#define SNN_HEAD_GOLDEN_TIMESTEPS 8U
#define SNN_HEAD_GOLDEN_INPUT_DIM 768U
#define SNN_HEAD_GOLDEN_HIDDEN_DIM 1536U
#define SNN_HEAD_GOLDEN_ACTION_DIM 7U

#define SNN_HEAD_GOLDEN_INPUT_ELEMS                                            \
    (SNN_HEAD_GOLDEN_TIMESTEPS * SNN_HEAD_GOLDEN_INPUT_DIM)
#define SNN_HEAD_GOLDEN_HIDDEN_ELEMS                                           \
    (SNN_HEAD_GOLDEN_TIMESTEPS * SNN_HEAD_GOLDEN_HIDDEN_DIM)
#define SNN_HEAD_GOLDEN_ACTION_ELEMS                                           \
    (SNN_HEAD_GOLDEN_TIMESTEPS * SNN_HEAD_GOLDEN_ACTION_DIM)

#ifdef __cplusplus
extern "C" {
#endif

/* 非 0 表示 snn_head_golden.c 已由 gen_snn_head_golden.py 填入真实 golden。 */
extern const int snn_head_golden_ready;

extern const float snn_head_golden_input[SNN_HEAD_GOLDEN_INPUT_ELEMS];
extern const float snn_head_golden_fc1_out[SNN_HEAD_GOLDEN_HIDDEN_ELEMS];
extern const float snn_head_golden_block0_out[SNN_HEAD_GOLDEN_HIDDEN_ELEMS];
extern const float snn_head_golden_block1_out[SNN_HEAD_GOLDEN_HIDDEN_ELEMS];
extern const float snn_head_golden_fc2_out[SNN_HEAD_GOLDEN_HIDDEN_ELEMS];
extern const float snn_head_golden_fc3_out[SNN_HEAD_GOLDEN_ACTION_ELEMS];

#ifdef __cplusplus
}
#endif

#endif /* SNN_HEAD_GOLDEN_H */
