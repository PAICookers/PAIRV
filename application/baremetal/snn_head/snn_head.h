#ifndef SNN_HEAD_H
#define SNN_HEAD_H

/*
 * SNN Head 公共 API。
 *
 * 对外只暴露 snn_head_run_chunk()：执行一个完整的 8-timestep action chunk。
 * 五层内部实现（fc1_lif / block_lif / fc2 / fc3）及其共享状态见 snn_head_internal.h，
 * 调用方（如 main.c）只需 include 本头文件。
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 执行一个完整的 8-timestep SNN Head action chunk。
 *
 * 顶层按层依次串起 fc1_lif -> block0 -> block1 -> fc2 -> fc3，中间结果全部在
 * 文件级共享 tensor_workspace 中覆盖式复用，最终写出 float32 action[8][7]。
 *
 * @param input 输入张量，形状 float32[8][768]。
 * @param action 输出动作，形状 float32[8][7]。
 * @return 整个 chunk 执行成功返回 true，否则返回 false。
 */
bool snn_head_run_chunk(const float *input, float *action);

#ifdef __cplusplus
}
#endif

#endif /* SNN_HEAD_H */
