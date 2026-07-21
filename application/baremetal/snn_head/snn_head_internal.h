#ifndef SNN_HEAD_INTERNAL_H
#define SNN_HEAD_INTERNAL_H

/*
 * SNN Head 分层实现的内部共享头：跨文件共享的宏、参数/artifact 符号、artifact 上下文
 * 与契约类型、共享 buffer、公共 helper 及各层入口声明。公共 API 见 snn_head.h。
 * 本头只做声明，共享 buffer 的唯一定义在 snn_head.c（此处仅 extern）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "artifact_reader.h"
#include "frame_codec.h"
#include "nn_layernorm.h"
#include "nn_quant.h"
#include "session.h"
#include "session_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SNN_HEAD_TIMESTEPS 8U
#define SNN_HEAD_INPUT_DIM 768U
#define SNN_HEAD_HIDDEN_DIM 1536U
#define SNN_HEAD_ACTION_DIM 7U
#define SNN_HEAD_INPUT_BYTES (SNN_HEAD_TIMESTEPS * SNN_HEAD_INPUT_DIM * sizeof(float))
#define SNN_HEAD_FC1_INPUT_ELEMENTS (SNN_HEAD_TIMESTEPS * SNN_HEAD_INPUT_DIM)
#define SNN_HEAD_TENSOR_WORKSPACE_BYTES (SNN_HEAD_TIMESTEPS * SNN_HEAD_HIDDEN_DIM * sizeof(float))
/* fc2 int32 膜电位每行 1536*4=6144 字节；int8 输入按此 stride 扩排，让本行 int32 输出
 * 原地覆盖同行已消费的 int8 输入而不踩下一行。 */
#define SNN_HEAD_VOLTAGE_STRIDE_BYTES (SNN_HEAD_HIDDEN_DIM * sizeof(int32_t))
/* 层间共享帧 buffer 容量，按单 timestep 最坏情况预留：VOLTAGE 层（fc2）每 int32 拆
 * 4 个 lane 帧 = 1536*4 + 1 = 6145。同一块 buffer 先当输入编码 workspace、发完后当
 * RX buffer，两用途时间不重叠。 */
#define SNN_HEAD_FRAME_BUF_FRAMES ((SNN_HEAD_HIDDEN_DIM * 4U) + 1U)
#define SNN_HEAD_TIMEOUT_MS 2000U
#define SNN_HEAD_ARTIFACT_TIMESTEPS 1U
#define SNN_HEAD_ARTIFACT_TICK_DEPTH 1U
#define SNN_HEAD_DTYPE_UINT1 1U
#define SNN_HEAD_DTYPE_INT32 9U

/* 逐层中间结果 dump（仅调试；默认关闭，对部署路径零影响）。定义 -DSNN_HEAD_ENABLE_DUMP
 * （board_test 用 make DUMP=1）后各层在阶段边界逐元素打印，未定义时展开为空语句。
 * sink 由 board_test/snn_head_dump_sink.c 提供。 */
typedef enum {
    SNN_HEAD_DUMP_F32 = 0, /* float32：IEEE-754 hex 位 + round(v*1e6) */
    SNN_HEAD_DUMP_S8 = 1,  /* int8 */
    SNN_HEAD_DUMP_S32 = 2, /* int32 */
    SNN_HEAD_DUMP_U8 = 3,  /* uint8（spike 字节 0/1） */
} snn_head_dump_dtype_t;

#ifdef SNN_HEAD_ENABLE_DUMP
void snn_head_dump_stage(const char *tag, const void *data, uint32_t count,
                         snn_head_dump_dtype_t dtype);
#define SNN_HEAD_DUMP(tag, data, count, dtype)                                 \
    snn_head_dump_stage((tag), (data), (uint32_t)(count), (dtype))
#else
#define SNN_HEAD_DUMP(tag, data, count, dtype) ((void)0)
#endif

/* LN 和量化参数（定义在 snn_head_params.c，放 .large_const_data 段，不占 DLM）。 */
extern const float snn_head_ln1_weight[SNN_HEAD_INPUT_DIM];
extern const float snn_head_ln1_bias[SNN_HEAD_INPUT_DIM];
extern const float snn_head_fc1_activation_scale;

extern const float snn_head_block0_ln_weight[SNN_HEAD_HIDDEN_DIM];
extern const float snn_head_block0_ln_bias[SNN_HEAD_HIDDEN_DIM];
extern const float snn_head_block0_activation_scale;
extern const float snn_head_block1_ln_weight[SNN_HEAD_HIDDEN_DIM];
extern const float snn_head_block1_ln_bias[SNN_HEAD_HIDDEN_DIM];
extern const float snn_head_block1_activation_scale;

/* LN2 仿射、fc2 输入 activation_scale，以及 li_out 膜电位 int32->fp32 反量化的
 * per-channel output_scale。 */
extern const float snn_head_ln2_weight[SNN_HEAD_HIDDEN_DIM];
extern const float snn_head_ln2_bias[SNN_HEAD_HIDDEN_DIM];
extern const float snn_head_fc2_activation_scale;
extern const float snn_head_fc2_output_scale[SNN_HEAD_HIDDEN_DIM];

/* fc3 输入 activation_scale，以及 acc_int32->fp32 action 反量化的 per-channel
 * output_scale（7 通道）。fc3 前无 LayerNorm。 */
extern const float snn_head_fc3_activation_scale;
extern const float snn_head_fc3_output_scale[SNN_HEAD_ACTION_DIM];

/* 各层 PAICore artifact 由构建系统嵌入 Flash，裸机侧只引用符号地址。 */
extern const uint8_t snn_head_fc1_lif_artifact_start[];
extern const uint8_t snn_head_fc1_lif_artifact_size[];
extern const uint8_t snn_head_block0_lif_artifact_start[];
extern const uint8_t snn_head_block0_lif_artifact_size[];
extern const uint8_t snn_head_block1_lif_artifact_start[];
extern const uint8_t snn_head_block1_lif_artifact_size[];
extern const uint8_t snn_head_fc2_artifact_start[];
extern const uint8_t snn_head_fc2_artifact_size[];
/* fc3 artifact 由 Makefile 在 fixture 到位后追加生成。 */
extern const uint8_t snn_head_fc3_artifact_start[];
extern const uint8_t snn_head_fc3_artifact_size[];

/*
 * 三块跨层共享 buffer 的唯一定义在 snn_head.c，此处仅声明（外部链接，单实例、落 .bss）。
 * tensor_workspace  : 主张量 workspace，五层共用，最大 float32/int32[8][1536] = 48KB，
 *                     底层 uint8_t，各层按阶段解释成 float/int8/int32，aligned(4)。
 * layer_frame_buf   : 层间共享帧 buffer，先当输入编码 workspace、发完后当 RX。
 * fc2_voltage_state : VOLTAGE 逐元素 lane 拼接状态，仅 fc2/fc3 用，每 timestep 前清零。
 */
extern uint8_t tensor_workspace[SNN_HEAD_TENSOR_WORKSPACE_BYTES];
extern rvrt_frame_t layer_frame_buf[SNN_HEAD_FRAME_BUF_FRAMES];
extern rvrt_voltage_decode_state_t fc2_voltage_state[SNN_HEAD_HIDDEN_DIM];

/**
 * @brief 单层 PAICore artifact 的读取结果上下文。
 *
 * 各字段借用 artifact backing bytes，使用期间不能释放或覆盖对应二进制。
 */
typedef struct snn_head_layer_artifact_context_s {
    rvrt_artifact_t artifact;                        /**< 非 owning artifact 句柄。 */
    rvrt_artifact_runtime_t runtime;                 /**< timestep/sync_steps/decode_mode 等。 */
    rvrt_artifact_input_mapping_view_t input_view;   /**< 输入 tensor 到 work frame 的映射。 */
    rvrt_artifact_output_mapping_view_t output_view; /**< 输出 frame 到逻辑 tensor 的映射。 */
} snn_head_layer_artifact_context_t;

/**
 * @brief 单层 artifact 必须满足的 SNN Head 静态契约，用于集成入口校验形状与语义。
 */
typedef struct snn_head_layer_artifact_contract_s {
    uint32_t input_entries;   /**< 单 timestep 输入 mapping entry 数。 */
    uint32_t input_bit_width; /**< 输入 payload bit width（均为 int8）。 */
    uint32_t output_entries;  /**< 输出 frame-address mapping entry 数。 */
    uint32_t output_elements; /**< 输出逻辑 tensor 元素数。 */
    uint32_t output_kind;     /**< 输出 kind：DATA 或 VOLTAGE。 */
    uint32_t output_dtype;    /**< 输出 dtype：UINT1 spike 或 INT32 membrane。 */
} snn_head_layer_artifact_contract_t;

/* block0/block1 拓扑同构共用同一份契约，由 run_chunk 传给 snn_head_run_block_lif，
 * 故跨文件可见（定义在 snn_head_block_lif.c）。FC1/FC2/FC3 契约留在各自 .c 内 static。 */
extern const snn_head_layer_artifact_contract_t SNN_HEAD_BLOCK_LIF_CONTRACT;

/**
 * @brief 读取单层 PAICore artifact 并借出 runtime 和 I/O mapping。定义在 snn_head.c。
 *
 * 只做最小读取，静态契约由 snn_head_validate_layer_artifact() 检查。
 *
 * @param artifact_start artifact 二进制起始地址（通常来自 Flash objcopy 符号）。
 * @param artifact_size_symbol objcopy 生成的 size 绝对符号地址。
 * @param context 接收 artifact 句柄、runtime、input view 和 output view。
 * @return 成功返回 true，否则返回 false。
 */
bool snn_head_read_layer_artifact(const uint8_t *artifact_start,
                                  const uint8_t *artifact_size_symbol,
                                  snn_head_layer_artifact_context_t *context);

/**
 * @brief 校验单层 artifact 是否满足固定分层契约。定义在 snn_head.c。
 *
 * 只判断 runtime 和 I/O mapping 的静态形状与语义，不访问 config frame 或权重内容。
 *
 * @param context 已由 snn_head_read_layer_artifact() 填好的上下文。
 * @param contract 当前层期望的输入/输出 mapping 契约。
 * @return 契约匹配返回 true，否则返回 false。
 */
bool snn_head_validate_layer_artifact(
    const snn_head_layer_artifact_context_t *context,
    const snn_head_layer_artifact_contract_t *contract);

/**
 * @brief 执行 fc1_lif 层，从浮点输入到 spike 输出。定义在 snn_head_fc1_lif.c。
 * @param input 外部输入张量，形状 float32[8][768]。
 * @return 成功返回 true；完成后 tensor_workspace 开头为 float32[8][1536]（spike 已拓宽）。
 */
bool snn_head_run_fc1_lif(const float *input);

/**
 * @brief 执行一个 block（block0 或 block1）。定义在 snn_head_block_lif.c。
 * @param ln_weight LN(1536) 的 gamma，长度 SNN_HEAD_HIDDEN_DIM。
 * @param ln_bias LN(1536) 的 beta，长度 SNN_HEAD_HIDDEN_DIM。
 * @param activation_scale 当前 block 的对称量化 activation_scale。
 * @param artifact_start 当前 block artifact 二进制起始地址。
 * @param artifact_size_symbol objcopy 生成的 size 绝对符号地址。
 * @param contract 当前 block artifact 必须满足的静态契约。
 * @return 成功返回 true；完成后 tensor_workspace 开头为 float32[8][1536]（spike 已拓宽）。
 */
bool snn_head_run_block_lif(
    const float *ln_weight, const float *ln_bias, float activation_scale,
    const uint8_t *artifact_start, const uint8_t *artifact_size_symbol,
    const snn_head_layer_artifact_contract_t *contract);

/**
 * @brief 执行 fc2 层（LN2 -> quant -> fc2 线性 -> li_out 膜电位）。定义在 snn_head_fc2.c。
 * @return 成功返回 true；完成后 tensor_workspace 开头为 float32[8][1536]（li_out 膜电位反量化）。
 */
bool snn_head_run_fc2(void);

/**
 * @brief 执行 fc3 层（quant -> fc3 线性 -> acc_int32 -> 反量化到 action）。定义在 snn_head_fc3.c。
 * @param action 输出动作缓冲区，形状 float32[8][7]，由本函数写入最终结果。
 * @return 成功返回 true，否则返回 false。
 */
bool snn_head_run_fc3(float *action);

#ifdef __cplusplus
}
#endif

#endif /* SNN_HEAD_INTERNAL_H */
