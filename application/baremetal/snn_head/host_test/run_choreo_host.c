/*
 * run_choreo_host.c —— host（x86）"编解码 + CPU 前处理"跨-timestep 一致性校验 driver。
 *
 * 全程 -fsanitize=address,undefined，跑真实层函数，两趟证明：
 *
 *  Pass A（distinct echo）—— 输出方向不串 + dtype 正确：
 *    每层 8 个 timestep 回显"各 t 不同"的已知输出，层跑完逐元素断言
 *    tensor_workspace[t][e] == 该 t 期望值。若 decode/扩排/拓宽把某 timestep 输出串到
 *    相邻行，或 dtype（spike 0/1、int32 反量化）算错，断言立刻抓出。
 *
 *  Pass B（uniform echo）—— 输入方向不被覆盖（你问的"当前输出不覆盖下一 timestep 输入"）：
 *    令每层 8 个 timestep 的输入逐字节相同（外部输入相同行 + 上一层 uniform 输出相同行），
 *    则每个 timestep 经 send_input_timestep 实际发出的输入帧序列必须完全一致。mock 捕获
 *    每轮发出的输入帧，断言 seg[t]==seg[0]。若某轮输出（decode/扩排/拓宽）在缓冲区内部
 *    悄悄改了后续 timestep 尚未发送的输入字节，那一轮发出的帧就会与第 0 段不同 -> 抓出。
 *    这是不需参考值、直接正面见证"输出未污染后续输入"的证明。
 *
 * 现存 fixtures 是过期的 timesteps=8；host_patch_artifact_single_step() 在内存里改成
 * timesteps=1/sync_steps=1，让真实层通过 validate 端到端跑。回显帧地址来自
 * host_enum_output_axons() 枚举的真实 (elem_idx -> axon_bit_idx)。
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
int snn_head_host_load_artifacts(const char *fixture_dir);
int host_patch_artifact_single_step(uint8_t *buf, unsigned size);
int host_enum_output_axons(const uint8_t *buf, unsigned size,
                           uint32_t *elem_to_axon, uint32_t max_elems,
                           uint32_t *out_count);
typedef uint32_t (*host_mock_gen_fn)(void *ctx, rvrt_frame_t *out, uint32_t cap);
void host_mock_set_generator(host_mock_gen_fn fn, void *ctx);
/* 发送输入帧捕获（Pass B）。 */
void host_mock_capture_begin(void);
void host_mock_capture_end(void);
uint32_t host_mock_capture_count(void);
bool host_mock_capture_overflow(void);
uint32_t host_mock_capture_len(uint32_t idx);
bool host_mock_capture_equal(uint32_t a, uint32_t b);

#ifndef SNN_HEAD_FIXTURE_DIR
#define SNN_HEAD_FIXTURE_DIR "fixtures"
#endif

/* 帧 high 位型（见 frame_codec.h）。 */
#define HOST_WORK_DATA_HIGH 0x80000000U
#define HOST_WORK_VOLTAGE_HIGH 0xA0000000U
#define HOST_COMPLETE_HIGH 0xE0000000U

/* 各层 artifact 精确字节数（与 CMake --defsym / shim 一致）。 */
#define FC1_ARTIFACT_SIZE 1301844U
#define BLK_ARTIFACT_SIZE 2535884U
#define FC2_ARTIFACT_SIZE 2536040U

/* VOLTAGE 反量化对拍相对容差。 */
#define CHOREO_VOLT_TOL 1e-3f

/* fc1 外部输入：8 个 timestep 逐字节相同（Pass B 需要），内容不影响回显决定的输出。 */
static float g_input[SNN_HEAD_TIMESTEPS * SNN_HEAD_INPUT_DIM];

/* ---- 回显生成器上下文（每层重建） ---- */
typedef struct {
    uint32_t elem_to_axon[SNN_HEAD_HIDDEN_DIM]; /* 枚举得到的元素->axon 映射 */
    uint32_t elems;                             /* 输出元素数（=1536） */
    bool is_voltage;                            /* true=VOLTAGE(fc2) */
    bool uniform;    /* true=所有 round 回显同一模式（Pass B 令各层输出各行相同） */
    uint32_t ctrl_calls;                        /* 内部：第几次控制屏障 */
} echo_ctx_t;

static echo_ctx_t g_echo;

static inline rvrt_frame_t mk_work(uint32_t high, uint32_t axon,
                                   uint8_t payload)
{
    const rvrt_frame_t f = {high, (axon << 8U) | (uint32_t)payload};
    return f;
}

/* DATA 层某 round 是否对元素 e 发射 spike：round 命中一组不相交的元素子集，使 8 个
 * timestep 输出行两两不同（Pass A 检出串扰）；uniform 时固定用 round 0 使各行相同。 */
static inline bool data_spike(uint32_t round, uint32_t e)
{
    return (e % SNN_HEAD_TIMESTEPS) == round;
}

/* VOLTAGE 层某 round 注入的 int32 膜电位值（各 round 不同、非零、跨多个字节）。 */
static inline int32_t volt_value(uint32_t round)
{
    return (int32_t)((round + 1U) * 1000U);
}

/* mock 自主回显生成器：ctrl_calls==0 -> reset（只 complete）；1..8 -> round 0..7。 */
static uint32_t choreo_gen(void *vctx, rvrt_frame_t *out, uint32_t cap)
{
    echo_ctx_t *const c = (echo_ctx_t *)vctx;
    const uint32_t idx = c->ctrl_calls++;
    uint32_t n = 0U;

    if (idx == 0U) { /* reset 屏障 */
        out[n++] = mk_work(HOST_COMPLETE_HIGH, 0U, 0U);
        return n;
    }
    const uint32_t round = idx - 1U;
    if (round >= SNN_HEAD_TIMESTEPS) {
        out[n++] = mk_work(HOST_COMPLETE_HIGH, 0U, 0U);
        return n;
    }
    /* uniform 模式：所有 round 用同一模式，使该层各 timestep 输出行逐字节相同，
     * 从而下一层各 timestep 输入相同，供 Pass B 的输入不变量断言。 */
    const uint32_t eff = c->uniform ? 0U : round;

    if (!c->is_voltage) {
        for (uint32_t e = 0U; (e < c->elems) && (n < cap - 1U); ++e) {
            if (data_spike(eff, e)) {
                out[n++] = mk_work(HOST_WORK_DATA_HIGH, c->elem_to_axon[e], 1U);
            }
        }
    } else {
        const uint32_t v = (uint32_t)volt_value(eff);
        for (uint32_t e = 0U; e < c->elems; ++e) {
            const uint32_t base = c->elem_to_axon[e];
            for (uint32_t lane = 0U; (lane < 4U) && (n < cap - 1U); ++lane) {
                const uint8_t payload = (uint8_t)(v >> (lane * 8U));
                out[n++] = mk_work(HOST_WORK_VOLTAGE_HIGH,
                                   base + lane * 8U, payload);
            }
        }
    }
    out[n++] = mk_work(HOST_COMPLETE_HIGH, 0U, 0U);
    return n;
}

static void host_debug_sink(rv_debug_level_t level, const char *title,
                            const char *function_name, const char *message,
                            void *user_data)
{
    (void)user_data;
    fprintf(stderr, "[rvdbg L%d] %s/%s: %s\n", (int)level,
            title ? title : "?", function_name ? function_name : "?",
            message ? message : "");
}

/* 为某层枚举 axon 映射并安装回显生成器（重置 round 计数）。 */
static bool arm_layer(const uint8_t *art_start, unsigned art_size,
                      bool is_voltage, bool uniform)
{
    uint32_t count = 0U;
    if (host_enum_output_axons(art_start, art_size, g_echo.elem_to_axon,
                               SNN_HEAD_HIDDEN_DIM, &count) != 0) {
        return false;
    }
    if (count != SNN_HEAD_HIDDEN_DIM) {
        fprintf(stderr, "unexpected output element count=%u (want %u)\n",
                (unsigned)count, (unsigned)SNN_HEAD_HIDDEN_DIM);
        return false;
    }
    g_echo.elems = count;
    g_echo.is_voltage = is_voltage;
    g_echo.uniform = uniform;
    g_echo.ctrl_calls = 0U;
    host_mock_set_generator(choreo_gen, &g_echo);
    return true;
}

/* ---- 被测层的统一调用包装（供两趟复用） ---- */
static bool run_fc1(void) { return snn_head_run_fc1_lif(g_input); }
static bool run_block0(void)
{
    return snn_head_run_block_lif(snn_head_block0_ln_weight,
                                  snn_head_block0_ln_bias,
                                  snn_head_block0_activation_scale,
                                  snn_head_block0_lif_artifact_start,
                                  snn_head_block0_lif_artifact_size,
                                  &SNN_HEAD_BLOCK_LIF_CONTRACT);
}
static bool run_block1(void)
{
    return snn_head_run_block_lif(snn_head_block1_ln_weight,
                                  snn_head_block1_ln_bias,
                                  snn_head_block1_activation_scale,
                                  snn_head_block1_lif_artifact_start,
                                  snn_head_block1_lif_artifact_size,
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
        case 0U: return snn_head_fc1_lif_artifact_start;
        case 1U: return snn_head_block0_lif_artifact_start;
        case 2U: return snn_head_block1_lif_artifact_start;
        default: return snn_head_fc2_artifact_start;
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
           ok ? "OK" : "MISMATCH", (unsigned)row0_spikes,
           (unsigned)mismatches);
    return ok ? 0 : 1;
}

/* Pass A：VOLTAGE 层输出 ≈ Vt * output_scale[e]。 */
static int verify_voltage(const char *name, const float *output_scale)
{
    const float *ws = (const float *)tensor_workspace;
    uint32_t mismatches = 0U;
    for (uint32_t t = 0U; t < SNN_HEAD_TIMESTEPS; ++t) {
        const float vt = (float)volt_value(t);
        for (uint32_t e = 0U; e < SNN_HEAD_HIDDEN_DIM; ++e) {
            const float got = ws[t * SNN_HEAD_HIDDEN_DIM + e];
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

/* Pass B：8 个 timestep 发出的输入帧必须逐段完全一致（输入未被输出污染）。 */
static int verify_input_invariance(const char *name)
{
    if (host_mock_capture_overflow()) {
        printf("  %-8s : CAPTURE OVERFLOW\n", name);
        return 1;
    }
    const uint32_t cnt = host_mock_capture_count();
    if (cnt != SNN_HEAD_TIMESTEPS) {
        printf("  %-8s : SEG COUNT=%u (want %u)\n", name, (unsigned)cnt,
               (unsigned)SNN_HEAD_TIMESTEPS);
        return 1;
    }
    const uint32_t len0 = host_mock_capture_len(0);
    uint32_t diff_ts = 0U;
    for (uint32_t t = 1U; t < cnt; ++t) {
        if (!host_mock_capture_equal(0U, t)) {
            ++diff_ts;
        }
    }
    const bool ok = (diff_ts == 0U) && (len0 > 0U);
    printf("  %-8s : %s  (frames/ts=%u diff_ts=%u)\n", name,
           ok ? "OK" : "MISMATCH", (unsigned)len0, (unsigned)diff_ts);
    return ok ? 0 : 1;
}

int main(void)
{
    rv_debug_set_sink(host_debug_sink, NULL);
    rv_debug_set_level(RV_DEBUG_DEBUG);

    if (snn_head_host_load_artifacts(SNN_HEAD_FIXTURE_DIR) != 0) {
        fprintf(stderr, "failed to load PAICore artifacts from %s\n",
                SNN_HEAD_FIXTURE_DIR);
        return 2;
    }

    /* 把 4 层过期 fixture 就地改成单步图（timesteps=1/sync_steps=1）。 */
    for (size_t i = 0U; i < LAYER_COUNT; ++i) {
        const int rc = host_patch_artifact_single_step(
            (uint8_t *)layer_start(i), LAYERS[i].size);
        if (rc != 0) {
            fprintf(stderr, "patch %s failed rc=%d\n", LAYERS[i].name, rc);
            return 2;
        }
    }

    /* fc1 外部输入：8 个 timestep 逐字节相同（各行同一向量，元素间取不同非零值）。 */
    for (uint32_t t = 0U; t < SNN_HEAD_TIMESTEPS; ++t) {
        for (uint32_t j = 0U; j < SNN_HEAD_INPUT_DIM; ++j) {
            g_input[t * SNN_HEAD_INPUT_DIM + j] =
                0.05f * (float)((int)(j % 17U) - 8);
        }
    }

    printf("== snn_head host choreography check (real layers, ASan/UBSan) ==\n");

    int fail = 0;

    /* -------- Pass A：输出方向不串 + dtype 正确（distinct echo） -------- */
    printf("[Pass A] output distinctness + dtype\n");
    for (size_t i = 0U; i < LAYER_COUNT; ++i) {
        if (!arm_layer(layer_start(i), LAYERS[i].size, LAYERS[i].is_voltage,
                       false) ||
            !LAYERS[i].run()) {
            printf("  %-8s : RUN FAILED\n", LAYERS[i].name);
            fail |= 1;
            continue;
        }
        if (LAYERS[i].is_voltage) {
            fail |= verify_voltage(LAYERS[i].name, snn_head_fc2_output_scale);
        } else {
            fail |= verify_data(LAYERS[i].name);
        }
    }

    /* -------- Pass B：当前输出不覆盖后续 timestep 输入（uniform echo） -------- */
    printf("[Pass B] input-not-overwritten (uniform echo, per-ts input equal)\n");
    for (size_t i = 0U; i < LAYER_COUNT; ++i) {
        host_mock_capture_begin();
        const bool armed =
            arm_layer(layer_start(i), LAYERS[i].size, LAYERS[i].is_voltage,
                      true);
        const bool ran = armed && LAYERS[i].run();
        host_mock_capture_end();
        if (!ran) {
            printf("  %-8s : RUN FAILED\n", LAYERS[i].name);
            fail |= 1;
            continue;
        }
        fail |= verify_input_invariance(LAYERS[i].name);
    }

    host_mock_set_generator(NULL, NULL);
    printf("\nHOST CHOREOGRAPHY: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
