#ifndef SNN_HEAD_H
#define SNN_HEAD_H

/*
 * SNN Head 公共 API。
 *
 * 对外只暴露 snn_head_run_chunk()：执行一个完整的 8-timestep action chunk。
 * 五层内部实现（fc1_lif / block_lif / fc2 / fc3）及其共享状态见
 * snn_head_internal.h， 调用方（如 main.c）只需 include 本头文件。
 */

#include <stdbool.h>
#include <stdint.h>

#define SNN_HEAD_TIMESTEPS 8U
#define SNN_HEAD_INPUT_DIM 768U
#define SNN_HEAD_ACTION_DIM 7U
#define SNN_HEAD_INPUT_FLOAT_COUNT (SNN_HEAD_TIMESTEPS * SNN_HEAD_INPUT_DIM)
#define SNN_HEAD_ACTION_FLOAT_COUNT (SNN_HEAD_TIMESTEPS * SNN_HEAD_ACTION_DIM)

typedef enum {
    SNN_HEAD_LAYER_FC1_LIF = 0,
    SNN_HEAD_LAYER_BLOCK0,
    SNN_HEAD_LAYER_BLOCK1,
    SNN_HEAD_LAYER_FC2,
    SNN_HEAD_LAYER_FC3,
    SNN_HEAD_LAYER_COUNT,
} snn_head_layer_id_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 执行一个完整的 8-timestep SNN Head action chunk。
 *
 * 顶层按层依次串起 fc1_lif -> block0 -> block1 -> fc2 -> fc3，中间结果全部在
 * 文件级共享 tensor_workspace 中覆盖式复用，最终写出 float32 action[8][7]。
 * 输入必须全部为有限值；UART 入口会在调用本函数前执行该校验。由于实现复用全局
 * workspace 和 PAICore session 状态，本函数不可重入，也不能并发调用。
 *
 * @param input 全部为有限值的输入张量，形状 float32[8][768]。
 * @param action 输出动作，形状 float32[8][7]。
 * @return 整个 chunk 执行成功返回 true，否则返回 false。
 */
bool snn_head_run_chunk(const float *input, float *action);

#ifdef __cplusplus
}
#endif

#endif /* SNN_HEAD_H */
