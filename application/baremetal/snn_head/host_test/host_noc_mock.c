/*
 * host_noc_mock.c —— 仅供 host（x86）运行时安全性测试使用的 NoC mock。
 *
 * 目的：让 SNN Head 各层在真实调用 PAICore 端"编码输入帧 / 解码输出帧"时，
 * 把真实数量、真实格式的帧喂进 runtime，配合 ASan/UBSan 压测编解码路径对共享
 * buffer（layer_frame_buf / tensor_workspace）的读写是否越界、覆盖、未对齐。
 * 本 mock 不校验数值，只负责把帧按协议投递给 ISR。
 *
 * 驱动原理（见 Lib/runtime/session.c）：
 *   - reset 和每个 sample 分段都使用独立的 control barrier；输入/config
 *     使用 unlocked 写。
 *   - 生成器只响应 Type-C/Type-D control；输入/config 帧不会产生 RX。
 *
 * 帧队列机制：
 *   - driver 在每次 sync_wait 前调用 host_mock_set_rx_frames() 预置一批待投递帧
 *     （一般是 work×N + 末尾 1 个 complete）。
 *   - 本次 active RX barrier 的控制帧发送时，ISR
 * 会把队列逐帧读走（read_frame_words 依次弹出）， handler 把它们追加进
 * session->rx_frames，读到 complete 即停。
 *   - 一次屏障消费完后队列复位；未预置队列时（如 reset_model）默认只投一个
 * complete， 用于纯粹推进屏障。
 */
#include "evalsoc_noc.h"
#include "frame_codec.h" /* rvrt_frame_t */
#include "nuclei_sdk_soc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* COMPLETE 帧：high 的 bit[31:28] == 0xE（见 RVRT_FRAME_KIND_OFFSET=28）。 */
#define HOST_MOCK_COMPLETE_HIGH 0xE0000000U

static void (*g_handler)(void);
static bool g_irq_enabled;
static rv_counter_t g_cycles;

/* 一次性 RX 帧队列（指向 driver 提供的数组，须存活到本次屏障消费完毕）。 */
static const rvrt_frame_t *g_rx_queue;
static uint32_t g_rx_len;
static uint32_t g_rx_pos;

/* driver 接口：预置下一次 active RX barrier 要投递的帧序列（末帧应为
 * complete）。 */
void host_mock_set_rx_frames(const rvrt_frame_t *frames, uint32_t count)
{
    g_rx_queue = frames;
    g_rx_len = count;
    g_rx_pos = 0U;
}

/*
 * 自主 echo 生成器（choreography 测试用）：
 *   runner 在层内先提交一个或多个 timestep，再以 inclusive target 同步。改由本
 * mock 在每次 active RX barrier（reset/sync）触发时主动调用生成器，并把原始
 * control frame 交给生成器。生成器据此区分 reset 和 inclusive
 * target，生成本段所有输出帧。 生成器返回帧数 n（含末尾 complete），写入
 * out[0..n-1]，n <= cap。
 */
typedef uint32_t (*host_mock_gen_fn)(void *ctx, uint32_t control_high,
                                     uint32_t control_low, rvrt_frame_t *out,
                                     uint32_t cap);
static host_mock_gen_fn g_gen;
static void *g_gen_ctx;
#define HOST_MOCK_GEN_CAP ((8U * 1536U * 4U) + 1U)
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

void noc_irq_enable(void)
{
    g_irq_enabled = true;
    if ((g_handler != NULL) && (noc_irq_pending() != 0U)) {
        g_handler();
    }
}
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

static void deliver_control(uint32_t high, uint32_t low)
{
    if (g_gen != NULL) {
        const uint32_t n =
            g_gen(g_gen_ctx, high, low, g_gen_buf, HOST_MOCK_GEN_CAP);
        g_rx_queue = g_gen_buf;
        g_rx_len = n;
        g_rx_pos = 0U;
    }
    if (g_irq_enabled && (g_handler != NULL)) {
        g_handler();
    }
}

void noc_fifo_write_frame_words_unlocked(uint32_t high, uint32_t low)
{
    const uint32_t type = high >> 28U;
    if (g_irq_enabled && ((type == 0xCU) || (type == 0xDU))) {
        deliver_control(high, low);
    }
}

void noc_fifo_write_frame_words(uint32_t high, uint32_t low)
{
    deliver_control(high, low);
}
