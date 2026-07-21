/*
 * host_noc_mock.c —— 仅供 host（x86）运行时安全性测试使用的 NoC mock。
 *
 * 目的：让 SNN Head 各层在真实调用 PAICore 端"编码输入帧 / 解码输出帧"时，
 * 把真实数量、真实格式的帧喂进 runtime，配合 ASan/UBSan 压测编解码路径对共享
 * buffer（layer_frame_buf / tensor_workspace）的读写是否越界、覆盖、未对齐。
 * 本 mock 不校验数值，只负责把帧按协议投递给 ISR。
 *
 * 驱动原理（见 Lib/runtime/session.c）：
 *   - 只有 reset_model 与 sync_wait 会 arm_phase 并用 noc_fifo_write_frame_words()
 *     发送一个控制帧；输入/config 走 *_unlocked，不触发 ISR、不消费 RX。
 *   - ISR paicore_noc_handler 仅在 phase.armed 时循环消费 RX，逐帧追加进 RX buffer，
 *     读到 COMPLETE 帧即置 done。
 *
 * 帧队列机制：
 *   - driver 在每次 sync_wait 前调用 host_mock_set_rx_frames() 预置一批待投递帧
 *     （一般是 work×N + 末尾 1 个 complete）。
 *   - 本次 armed 控制帧发送时，ISR 会把队列逐帧读走（read_frame_words 依次弹出），
 *     handler 把它们追加进 session->rx_frames，读到 complete 即停。
 *   - 一次屏障消费完后队列复位；未预置队列时（如 reset_model）默认只投一个 complete，
 *     用于纯粹推进屏障。
 */
#include "evalsoc_noc.h"
#include "frame_codec.h" /* rvrt_frame_t */
#include "nuclei_sdk_soc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* COMPLETE 帧：high 的 bit[31:28] == 0xE（见 RVRT_FRAME_KIND_OFFSET=28）。 */
#define HOST_MOCK_COMPLETE_HIGH 0xE0000000U

static void (*g_handler)(void);
static bool g_irq_enabled;
static rv_counter_t g_cycles;

/* 一次性 RX 帧队列（指向 driver 提供的数组，须存活到本次屏障消费完毕）。 */
static const rvrt_frame_t *g_rx_queue;
static uint32_t g_rx_len;
static uint32_t g_rx_pos;

/* driver 接口：预置下一次 armed 屏障要投递的帧序列（末帧应为 complete）。 */
void host_mock_set_rx_frames(const rvrt_frame_t *frames, uint32_t count)
{
    g_rx_queue = frames;
    g_rx_len = count;
    g_rx_pos = 0U;
}

/*
 * 自主 echo 生成器（choreography 测试用）：
 *   真实层的 8 轮 timestep 循环在层函数内部，driver 无法在 send 与 sync 之间逐轮
 *   灌帧。改由本 mock 在每次 armed 控制屏障（reset/sync）触发时主动调用生成器，
 *   由其依据"第几次屏障"自主回显本轮应产出的输出帧。生成器需自行在 ctx 内计数：
 *   第 0 次 = reset（只回一个 complete 推进屏障），第 1..8 次 = sync round 0..7。
 *   生成器返回帧数 n（含末尾 complete），写入 out[0..n-1]，n <= cap。
 */
typedef uint32_t (*host_mock_gen_fn)(void *ctx, rvrt_frame_t *out, uint32_t cap);
static host_mock_gen_fn g_gen;
static void *g_gen_ctx;
#define HOST_MOCK_GEN_CAP 6145U
static rvrt_frame_t g_gen_buf[HOST_MOCK_GEN_CAP];

/* driver 接口：设置/清除自主生成器；ctx 由生成器自身用于计数与查表。 */
void host_mock_set_generator(host_mock_gen_fn fn, void *ctx)
{
    g_gen = fn;
    g_gen_ctx = ctx;
    g_rx_queue = NULL;
    g_rx_len = 0U;
    g_rx_pos = 0U;
}

/*
 * 发送输入帧捕获（choreography 输入不变量见证用）：
 *   send_input_timestep 把每个输入帧经 noc_fifo_write_frame_words_unlocked() 逐帧发出，
 *   sync_wait 的 armed 屏障（locked 写）是每个 timestep 的边界。本捕获把
 *   "两次 locked 写之间累积的 unlocked 帧"切成一段，即该 timestep 实际发出的输入帧。
 *   第一个 locked 写是 reset（此前是 config 帧，丢弃）；其后每个 sync 各存一段。
 *   driver 令 8 个 timestep 输入逐字节相同 -> 8 段应完全一致；若某轮输出覆盖了后续
 *   timestep 的输入，则该轮发出的输入帧会与第 0 段不同，被 host_mock_capture_equal() 抓出。
 */
#define HOST_MOCK_MAX_SEG 8U
#define HOST_MOCK_MAX_SEG_FRAMES 2048U
static bool g_cap_enabled;
static bool g_cap_seen_reset;
static rvrt_frame_t g_cap_cur[HOST_MOCK_MAX_SEG_FRAMES];
static uint32_t g_cap_cur_len;
static bool g_cap_overflow;
static rvrt_frame_t g_cap_seg[HOST_MOCK_MAX_SEG][HOST_MOCK_MAX_SEG_FRAMES];
static uint32_t g_cap_seg_len[HOST_MOCK_MAX_SEG];
static uint32_t g_cap_seg_count;

void host_mock_capture_begin(void)
{
    g_cap_enabled = true;
    g_cap_seen_reset = false;
    g_cap_cur_len = 0U;
    g_cap_overflow = false;
    g_cap_seg_count = 0U;
}

void host_mock_capture_end(void) { g_cap_enabled = false; }

uint32_t host_mock_capture_count(void) { return g_cap_seg_count; }
bool host_mock_capture_overflow(void) { return g_cap_overflow; }
uint32_t host_mock_capture_len(uint32_t idx)
{
    return (idx < g_cap_seg_count) ? g_cap_seg_len[idx] : 0U;
}

/* 段 a 与段 b 的帧序列是否逐帧完全相同。 */
bool host_mock_capture_equal(uint32_t a, uint32_t b)
{
    if ((a >= g_cap_seg_count) || (b >= g_cap_seg_count)) {
        return false;
    }
    if (g_cap_seg_len[a] != g_cap_seg_len[b]) {
        return false;
    }
    return memcmp(g_cap_seg[a], g_cap_seg[b],
                  (size_t)g_cap_seg_len[a] * sizeof(rvrt_frame_t)) == 0;
}

int32_t ECLIC_Register_IRQ(int32_t irqn, uint8_t shv, uint8_t trigger,
                           uint8_t level, uint8_t priority, void *handler)
{
    (void)irqn;
    (void)shv;
    (void)trigger;
    (void)level;
    (void)priority;
    g_handler = (void (*)(void))handler;
    return 0;
}

rv_counter_t __get_rv_cycle(void) { return ++g_cycles; }

void noc_irq_enable(void) { g_irq_enabled = true; }
void noc_irq_disable(void) { g_irq_enabled = false; }
void noc_irq_ack(void) {}
bool noc_irq_is_enabled(void) { return g_irq_enabled; }
uint32_t noc_irq_pending(void)
{
    return ((g_rx_queue != NULL) && (g_rx_pos < g_rx_len)) ? 1U : 0U;
}

int32_t noc_fifo_read_frame_words(uint32_t *high, uint32_t *low)
{
    if ((high == NULL) || (low == NULL)) {
        return -1;
    }
    if ((g_rx_queue != NULL) && (g_rx_pos < g_rx_len)) {
        *high = g_rx_queue[g_rx_pos].high;
        *low = g_rx_queue[g_rx_pos].low;
        ++g_rx_pos;
        return 0;
    }
    /* 队列未设置或已耗尽：投递一个 complete 帧，干净地终止本次屏障。 */
    *high = HOST_MOCK_COMPLETE_HIGH;
    *low = 0U;
    return 0;
}

/* config / 输入分块发送走这里：不触发 ISR，直接丢弃；捕获开启时累积当前段。 */
void noc_fifo_write_frame_words_unlocked(uint32_t high, uint32_t low)
{
    if (g_cap_enabled && g_cap_seen_reset) {
        if (g_cap_cur_len < HOST_MOCK_MAX_SEG_FRAMES) {
            g_cap_cur[g_cap_cur_len].high = high;
            g_cap_cur[g_cap_cur_len].low = low;
            ++g_cap_cur_len;
        } else {
            g_cap_overflow = true;
        }
    }
}

/* armed 控制帧发送（reset / sync）：同步触发 ISR，让它把队列帧消费进 RX buffer。 */
void noc_fifo_write_frame_words(uint32_t high, uint32_t low)
{
    (void)high;
    (void)low;
    /* 捕获：locked 写是 timestep 边界。首个 locked=reset（丢弃此前 config 段），
     * 其后每个 sync 把累积段落存为该 timestep 的输入帧。 */
    if (g_cap_enabled) {
        if (!g_cap_seen_reset) {
            g_cap_seen_reset = true;
            g_cap_cur_len = 0U;
        } else {
            if (g_cap_seg_count < HOST_MOCK_MAX_SEG) {
                uint32_t n = g_cap_cur_len;
                if (n > HOST_MOCK_MAX_SEG_FRAMES) {
                    n = HOST_MOCK_MAX_SEG_FRAMES;
                }
                memcpy(g_cap_seg[g_cap_seg_count], g_cap_cur,
                       (size_t)n * sizeof(rvrt_frame_t));
                g_cap_seg_len[g_cap_seg_count] = n;
                ++g_cap_seg_count;
            } else {
                g_cap_overflow = true;
            }
            g_cap_cur_len = 0U;
        }
    }
    /* 自主生成器模式：本次屏障的待投递帧由生成器现场产出。 */
    if (g_gen != NULL) {
        const uint32_t n = g_gen(g_gen_ctx, g_gen_buf, HOST_MOCK_GEN_CAP);
        g_rx_queue = g_gen_buf;
        g_rx_len = n;
        g_rx_pos = 0U;
    }
    if (g_irq_enabled && (g_handler != NULL)) {
        g_handler();
    }
    /* 本次屏障已消费队列，复位为默认（下次未预置则只投 complete）。 */
    g_rx_queue = NULL;
    g_rx_len = 0U;
    g_rx_pos = 0U;
}
