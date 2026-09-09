/*
 * run_choreo_host.c —— host（x86）"编解码 + CPU 前处理"跨-timestep 一致性校验
 * driver。
 *
 * 全程 -fsanitize=address,undefined，跑真实层函数和一趟 distinct synthetic
 * output，验证输出方向不串且 dtype 正确：
 *
 *    每层 8 个 timestep 回显"各 t 不同"的已知输出，层跑完逐元素断言
 *    tensor_workspace[t][e] == 该 t 期望值。若 decode/扩排/拓宽把某 timestep
 * 输出串到 相邻行，或 dtype（spike 0/1、int32 反量化）算错，断言立刻抓出。
 *    PAICore 数值输出由 NoC mock 生成，因此本测试不建立 numerical golden 证据。
 *
 * assets 保持真实 timesteps=8；completion target 是完整样本的 control
 * timeline target，不受 output mapping 物理地址残量影响。runner
 * 分段提交输入并发送增量 control payload，IRQ 将 work
 * frame 流式解码到输出。 回显帧地址来自 host_enum_output_axons() 枚举的真实
 * (elem_idx -> axon_bit_idx)。
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "snn_head_internal.h"

/* ---- host 侧外部符号（shim / mock / C++ 辅助） ---- */
int snn_head_host_load_artifacts(const char *asset_dir);
int host_enum_output_axons(const uint8_t *buf, unsigned size,
                           uint32_t *elem_to_axon, uint32_t max_elems,
                           uint32_t *out_count);
typedef uint32_t (*host_mock_gen_fn)(void *ctx, uint32_t control_high,
                                     uint32_t control_low, rvrt_frame_t *out,
                                     uint32_t cap);
void host_mock_set_generator(host_mock_gen_fn fn, void *ctx);

#ifndef SNN_HEAD_ASSET_DIR
#define SNN_HEAD_ASSET_DIR "assets"
#endif

/* 帧 high 位型（见 frame_codec.h）。 */
#define HOST_WORK_DATA_HIGH 0x80000000U
#define HOST_WORK_VOLTAGE_HIGH 0xA0000000U
#define HOST_COMPLETE_HIGH 0xE0000000U

/* 各层 artifact 精确字节数（与 CMake --defsym / shim 一致）。 */
#define FC1_ARTIFACT_SIZE 1310992U
#define BLK_ARTIFACT_SIZE 2541968U
#define FC2_ARTIFACT_SIZE 2535968U
#define FC3_ARTIFACT_SIZE 85088U

/* VOLTAGE 反量化对拍相对容差。 */
#define CHOREO_VOLT_TOL 1e-3f

/* fc1 外部输入内容不影响回显决定的输出。 */
static float g_input[SNN_HEAD_TIMESTEPS * SNN_HEAD_INPUT_DIM];
static float g_action[SNN_HEAD_TIMESTEPS * SNN_HEAD_ACTION_DIM];

/* ---- 回显生成器上下文（每层重建） ---- */
typedef struct {
    uint32_t elem_to_axon[SNN_HEAD_HIDDEN_DIM]; /* 枚举得到的元素->axon 映射 */
    uint32_t elems;             /* 输出元素数（fc2=1536，fc3=7） */
    bool is_voltage;            /* true=VOLTAGE（fc2/fc3） */
    uint32_t next_timestep;     /* 下一个尚未生成的逻辑行 */
    uint32_t timesteps;         /* artifact 的完整样本长度 */
    uint32_t completion_target; /* artifact 的最终 control timeline target */
    uint32_t current_target;    /* 已累计接收的 control timeline target。 */
    uint32_t output_target_lcn; /* 输出 frame timestamp 的 LCN。 */
} echo_ctx_t;

static echo_ctx_t g_echo;

static inline rvrt_frame_t mk_work(uint32_t high, uint32_t axon,
                                   uint32_t timestamp, uint8_t payload)
{
    const rvrt_frame_t f = {
        high | (((timestamp >> 7U) & 0x1U) << 28U),
        ((timestamp & 0x7FU) << 17U) | (axon << 8U) | (uint32_t)payload,
    };
    return f;
}

/* DATA 层某 round 是否对元素 e 发射 spike：round 命中一组不相交的元素子集，使 8
 * 个 timestep 输出行两两不同，供 Pass A 检出串扰。 */
static inline bool data_spike(uint32_t round, uint32_t e)
{
    return (e % SNN_HEAD_TIMESTEPS) == round;
}

/* VOLTAGE 层某 round 注入的 int32 膜电位值（各 round 不同、非零、跨多个字节）。
 */
static inline int32_t volt_value(uint32_t round)
{
    return (int32_t)((round + 1U) * 1000U);
}

/* mock 自主回显生成器：reset 只 complete；sync payload 是增量 target。 */
static uint32_t choreo_gen(void *vctx, uint32_t control_high,
                           uint32_t control_low, rvrt_frame_t *out,
                           uint32_t cap)
{
    echo_ctx_t *const c = (echo_ctx_t *)vctx;
    uint32_t n = 0U;

    if ((control_high >> 28U) == 0xDU) { /* reset 控制帧 */
        c->next_timestep = 0U;
        c->current_target = 0U;
        out[n++] = mk_work(HOST_COMPLETE_HIGH, 0U, 0U, 0U);
        return n;
    }
    if ((control_high >> 28U) != 0xCU) {
        return 0U;
    }

    const uint32_t delta = control_low & 0xFFFFFFU;
    if ((delta == 0U) || (delta > c->completion_target) ||
        (c->current_target > c->completion_target - delta)) {
        return 0U;
    }
    const uint32_t target = c->current_target + delta;

    for (uint32_t timestep = c->next_timestep;
         (timestep < target) && (timestep < c->timesteps); ++timestep) {
        const uint32_t effective_timestep = timestep;
        const uint32_t timestamp = timestep << c->output_target_lcn;
        if (!c->is_voltage) {
            for (uint32_t e = 0U; (e < c->elems) && (n < cap - 1U); ++e) {
                if (data_spike(effective_timestep, e)) {
                    out[n++] = mk_work(HOST_WORK_DATA_HIGH, c->elem_to_axon[e],
                                       timestamp, 1U);
                }
            }
        } else {
            const uint32_t value = (uint32_t)volt_value(effective_timestep);
            for (uint32_t e = 0U; e < c->elems; ++e) {
                const uint32_t base = c->elem_to_axon[e];
                for (uint32_t lane = 0U; (lane < 4U) && (n < cap - 1U);
                     ++lane) {
                    const uint8_t payload = (uint8_t)(value >> (lane * 8U));
                    out[n++] = mk_work(HOST_WORK_VOLTAGE_HIGH, base + lane * 8U,
                                       timestamp, payload);
                }
            }
        }
    }
    c->next_timestep = target;
    c->current_target = target;
    out[n++] = mk_work(HOST_COMPLETE_HIGH, 0U, 0U, 0U);
    return n;
}

static void host_debug_sink(rv_debug_level_t level, const char *title,
                            const char *function_name, const char *message,
                            void *user_data)
{
    (void)user_data;
    fprintf(stderr, "[rvdbg L%d] %s/%s: %s\n", (int)level, title ? title : "?",
            function_name ? function_name : "?", message ? message : "");
}

/* 为某层枚举 axon 映射并安装回显生成器（重置 round 计数）。 */
static bool arm_layer(const uint8_t *art_start, unsigned art_size,
                      uint32_t output_elements, bool is_voltage)
{
    uint32_t count = 0U;
    if (host_enum_output_axons(art_start, art_size, g_echo.elem_to_axon,
                               SNN_HEAD_HIDDEN_DIM, &count) != 0) {
        return false;
    }
    if (count != output_elements) {
        fprintf(stderr, "unexpected output element count=%u (want %u)\n",
                (unsigned)count, (unsigned)output_elements);
        return false;
    }
    g_echo.elems = count;
    g_echo.is_voltage = is_voltage;
    rvrt_artifact_t artifact = {0};
    rvrt_artifact_output_mapping_view_t output_view = {0};
    rvrt_artifact_runtime_t runtime = {0};
    if ((rvrt_artifact_read(art_start, art_size, &artifact) !=
         RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_get_output_mapping_view(
             &artifact, 0U, 0U, &output_view) != RVRT_ARTIFACT_OK) ||
        (rvrt_artifact_thread_runtime(&artifact, 0U, &runtime) !=
         RVRT_ARTIFACT_OK)) {
        return false;
    }
    g_echo.next_timestep = 0U;
    g_echo.current_target = 0U;
    g_echo.timesteps = runtime.timesteps;
    g_echo.completion_target = runtime.completion_sync_timestep;
    g_echo.output_target_lcn = output_view.target_lcn;
    host_mock_set_generator(choreo_gen, &g_echo);
    return true;
}

/* ---- 被测层的统一调用包装（供两趟复用） ---- */
static bool run_fc1(void) { return snn_head_run_fc1_lif(g_input); }
static bool run_block0(void)
{
    return snn_head_run_block_lif(
        snn_head_block0_ln_weight, snn_head_block0_ln_bias,
        SNN_HEAD_LAYER_BLOCK0, snn_head_block0_activation_scale,
        snn_head_block0_lif_artifact_start, snn_head_block0_lif_artifact_size,
        &SNN_HEAD_BLOCK_LIF_CONTRACT);
}
static bool run_block1(void)
{
    return snn_head_run_block_lif(
        snn_head_block1_ln_weight, snn_head_block1_ln_bias,
        SNN_HEAD_LAYER_BLOCK1, snn_head_block1_activation_scale,
        snn_head_block1_lif_artifact_start, snn_head_block1_lif_artifact_size,
        &SNN_HEAD_BLOCK_LIF_CONTRACT);
}
static bool run_fc2_(void) { return snn_head_run_fc2(); }

typedef bool (*run_fn_t)(void);
typedef struct {
    const char *name;
    run_fn_t run;
    const uint8_t *start;
    unsigned size;
    bool is_voltage;
} layer_desc_t;

static const layer_desc_t LAYERS[] = {
    {"fc1_lif", run_fc1, NULL, FC1_ARTIFACT_SIZE, false},
    {"block0", run_block0, NULL, BLK_ARTIFACT_SIZE, false},
    {"block1", run_block1, NULL, BLK_ARTIFACT_SIZE, false},
    {"fc2", run_fc2_, NULL, FC2_ARTIFACT_SIZE, true},
};
#define LAYER_COUNT (sizeof(LAYERS) / sizeof(LAYERS[0]))

/* start 符号在运行期才是最终地址；集中在此取，避免静态初始化顺序问题。 */
static const uint8_t *layer_start(size_t i)
{
    switch (i) {
        case 0U:
            return snn_head_fc1_lif_artifact_start;
        case 1U:
            return snn_head_block0_lif_artifact_start;
        case 2U:
            return snn_head_block1_lif_artifact_start;
        default:
            return snn_head_fc2_artifact_start;
    }
}

/* Pass A：DATA 层输出精确等于该轮 spike 模式的 0.0/1.0。 */
static int verify_data(const char *name)
{
    const float *ws = (const float *)tensor_workspace;
    uint32_t mismatches = 0U;
    uint32_t row0_spikes = 0U;
    for (uint32_t t = 0U; t < SNN_HEAD_TIMESTEPS; ++t) {
        for (uint32_t e = 0U; e < SNN_HEAD_HIDDEN_DIM; ++e) {
            const float got = ws[t * SNN_HEAD_HIDDEN_DIM + e];
            const float want = data_spike(t, e) ? 1.0f : 0.0f;
            if (got != want) {
                ++mismatches;
            }
            if ((t == 0U) && (got == 1.0f)) {
                ++row0_spikes;
            }
        }
    }
    const bool ok = (mismatches == 0U) && (row0_spikes > 0U);
    printf("  %-8s : %s  (row0_spikes=%u mismatches=%u)\n", name,
           ok ? "OK" : "MISMATCH", (unsigned)row0_spikes, (unsigned)mismatches);
    return ok ? 0 : 1;
}

/* Pass A：VOLTAGE 层输出 ≈ Vt * output_scale[e]。 */
static int verify_voltage(const char *name, const float *actual,
                          uint32_t elements, const float *output_scale)
{
    uint32_t mismatches = 0U;
    for (uint32_t t = 0U; t < SNN_HEAD_TIMESTEPS; ++t) {
        const float vt = (float)volt_value(t);
        for (uint32_t e = 0U; e < elements; ++e) {
            const float got = actual[t * elements + e];
            const float want = vt * output_scale[e];
            const float tol = CHOREO_VOLT_TOL * (fabsf(want) + 1e-6f);
            if (fabsf(got - want) > tol) {
                ++mismatches;
            }
        }
    }
    const bool ok = (mismatches == 0U);
    printf("  %-8s : %s  (mismatches=%u)\n", name, ok ? "OK" : "MISMATCH",
           (unsigned)mismatches);
    return ok ? 0 : 1;
}

int main(void)
{
    rv_debug_set_sink(host_debug_sink, NULL);
    rv_debug_set_level(RV_DEBUG_DEBUG);

    if (snn_head_host_load_artifacts(SNN_HEAD_ASSET_DIR) != 0) {
        fprintf(stderr, "failed to load PAICore artifacts from %s\n",
                SNN_HEAD_ASSET_DIR);
        return 2;
    }

    /* fc1 外部输入：各行同一向量，元素间取不同非零值。 */
    for (uint32_t t = 0U; t < SNN_HEAD_TIMESTEPS; ++t) {
        for (uint32_t j = 0U; j < SNN_HEAD_INPUT_DIM; ++j) {
            g_input[t * SNN_HEAD_INPUT_DIM + j] =
                0.05f * (float)((int)(j % 17U) - 8);
        }
    }

    printf(
        "== snn_head host choreography check (real layers, ASan/UBSan) ==\n");

    int fail = 0;

    /* -------- Pass A：输出方向不串 + dtype 正确（distinct echo） -------- */
    printf("[Pass A] output distinctness + dtype\n");
    for (size_t i = 0U; i < LAYER_COUNT; ++i) {
        if (!arm_layer(layer_start(i), LAYERS[i].size, SNN_HEAD_HIDDEN_DIM,
                       LAYERS[i].is_voltage) ||
            !LAYERS[i].run()) {
            printf("  %-8s : RUN FAILED\n", LAYERS[i].name);
            fail |= 1;
            continue;
        }
        if (LAYERS[i].is_voltage) {
            fail |=
                verify_voltage(LAYERS[i].name, (const float *)tensor_workspace,
                               SNN_HEAD_HIDDEN_DIM, snn_head_fc2_output_scale);
        } else {
            fail |= verify_data(LAYERS[i].name);
        }
    }

    if (!arm_layer(snn_head_fc3_artifact_start, FC3_ARTIFACT_SIZE,
                   SNN_HEAD_ACTION_DIM, true) ||
        !snn_head_run_fc3(g_action)) {
        printf("  fc3     : RUN FAILED\n");
        fail |= 1;
    } else {
        fail |= verify_voltage("fc3", g_action, SNN_HEAD_ACTION_DIM,
                               snn_head_fc3_output_scale);
    }

    host_mock_set_generator(NULL, NULL);
    printf("\nHOST CHOREOGRAPHY: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
