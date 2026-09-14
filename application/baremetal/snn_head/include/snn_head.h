#ifndef SNN_HEAD_H
#define SNN_HEAD_H

/*
 * SNN Head 公共 API。
 *
 * 普通构建对外暴露 snn_head_run_chunk()，执行一个完整的 8-timestep
 * action chunk。SNN_HEAD_RESIDENT=1 时还暴露常驻参数初始化与状态查询接口；
 * SNN_HEAD_AUDIT=1 时额外暴露诊断性驻留校验接口。五层内部实现
 * （fc1_lif / block_lif / fc2 / fc3）及其共享状态见 snn_head_internal.h，
 * 调用方（如 main.c）只需 include 本头文件。
 */

#include <stdbool.h>
#include <stdint.h>

#ifndef SNN_HEAD_RESIDENT
#define SNN_HEAD_RESIDENT 0
#endif
#if SNN_HEAD_RESIDENT != 0 && SNN_HEAD_RESIDENT != 1
#error "SNN_HEAD_RESIDENT must be 0 or 1"
#endif
#ifndef SNN_HEAD_AUDIT
#define SNN_HEAD_AUDIT 0
#endif
#if SNN_HEAD_AUDIT && !SNN_HEAD_RESIDENT
#error "SNN_HEAD_AUDIT requires resident assets"
#endif

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

#if SNN_HEAD_RESIDENT
typedef struct {
    uint64_t total_cycles;
    uint64_t prepare_cycles;
    uint64_t load_cycles;
    uint64_t audit_cycles;
    uint32_t config_frames;
    bool ready;
} snn_head_initialization_stats_t;

/** Load all five disjoint resident groups before accepting a request.
 * Successful calls are idempotent. A failed initialization or inference
 * requires platform recovery and a fresh application start; it is not retried.
 */
bool snn_head_initialize(void);
const snn_head_initialization_stats_t *snn_head_initialization_stats(void);
#if SNN_HEAD_AUDIT
/** Diagnostic-only readback of immutable weights in the current deployment. */
bool snn_head_verify_residency(void);
#endif
#endif

/**
 * @brief 执行一个完整的 8-timestep SNN Head action chunk。
 *
 * 顶层按层依次串起 fc1_lif -> block0 -> block1 -> fc2 -> fc3，中间结果全部在
 * 文件级共享 tensor_workspace 中覆盖式复用，最终写出 float32 action[8][7]。
 * 输入必须全部为有限值；UART 入口会在调用本函数前执行该校验。由于实现复用全局
 * workspace 和 PAICore session 状态，本函数不可重入，也不能并发调用。
 * 三片常驻构建要求先成功调用 snn_head_initialize()；每层仍逐 chunk INIT，
 * 但不重新加载参数。任一推理失败后拒绝后续请求，等待平台恢复。
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
