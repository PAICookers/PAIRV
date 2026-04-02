#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "nuclei_sdk_soc.h"
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "frames.h"

// ================== SPI定义 ==================
#define SPI_BASE ((uint32_t volatile *)0x10014000)
#define SPI_REG32(p, i) (*(volatile uint32_t *)((p) + (i)))
#define SPI_REG(offset) SPI_REG32(SPI_BASE, offset)

#define SPI_REG_SCKMODE 0x04
#define SPI_REG_FORCE 0x0c
#define SPI_REG_FMT 0x40
#define SPI_REG_FCTRL 0x60
#define SPI_REG_CSDEF 0x14
#define SPI_REG_SCKDIV 0x00
#define SPI_REG_CSID 0x10
#define SPI_REG_CSMODE 0x18
#define SPI_REG_FFMT 0x64
#define SPI_FFMT1 0x78
#define SPI_REG_STATUS 0x7C
#define SPI_REG_TXFIFO 0x48
#define SPI_REG_RXFIFO 0x4c
#define SPI_CR 0x84

void SPI_Init(void)
{
    SPI_REG(SPI_REG_SCKDIV) = 3;
}

// ================== 硬件定义 ==================
// SNN相关硬件定义
#define REG_soc_irq0_REG 0x10000000
#define FIFO_DOWN_ADDR 0x10008000
#define FIFO_UP_ADDR 0x10010000
#define CLIC_CLILINTIE_BASE 0x18020000
#define SNN_CLILINTIE_OFF 0x10bd

uint64_t start_time = 0;
uint64_t end_time = 0;
uint64_t start_compute_time = 0;
uint64_t end_compute_time = 0;

// 硬件寄存器指针
volatile unsigned long *snn_irq_reg = (unsigned long *)REG_soc_irq0_REG;
volatile uint8_t *snn_clilintie = (uint8_t *)(CLIC_CLILINTIE_BASE + SNN_CLILINTIE_OFF);

// FIFO地址
volatile unsigned long *mem_up_base = (unsigned long *)FIFO_UP_ADDR;
volatile unsigned long *mem_down_base = (unsigned long *)FIFO_DOWN_ADDR;

// 根据中的CFG_TMR_BASE_ADDR和N300数据手册，TIMER基地址为0x18030000
#define TIMER_BASE 0x18030000                                      // TIMER单元基地址
#define MTIME_REG (*((volatile uint64_t *)(TIMER_BASE + 0x00)))    // mtime寄存器
#define MTIMECMP_REG (*((volatile uint64_t *)(TIMER_BASE + 0x08))) // mtimecmp寄存器

// 超时标志 - 必须使用volatile，因为会在中断中被修改
volatile uint8_t snn_timeout_flag = 0;

// ================== UART接收配置 ==================
// 注意：波特率已在system_evalsoc.c中设置为3000000
#define UART_RX_BUFFER_SIZE 512 // 接收缓冲区大小
#define UART_FIFO_WATERMARK 1   // 每收到1字节触发中断

// UART接收环形缓冲区结构
typedef struct
{
    uint8_t buffer[UART_RX_BUFFER_SIZE];
    volatile uint16_t head;  // 写入位置（中断中修改）
    volatile uint16_t tail;  // 读取位置（主循环中修改）
    volatile uint16_t count; // 当前数据量
} uart_rx_buffer_t;

// 全局接收缓冲区
static uart_rx_buffer_t uart_rx_buf = {0};

// ================== 命令处理配置 ==================
#define MAX_CMD_LENGTH 128 // 最大命令长度

// ================== SNN相关变量 ==================
// 64位数据结构
typedef struct
{
    uint32_t high;
    uint32_t low;
} uint64_data_t;

// 中断数据缓冲区
volatile uint64_data_t data_buffer[1];
volatile uint32_t data_count = 0;
volatile uint32_t finish_count = 0;
volatile uint8_t data_ready = 0;
volatile uint32_t data_packet_number = 0;

// ================== 可变大小的config和input帧 ==================
// 使用指针和长度变量代替固定大小的数组
#define CONFIG_FRAME_SIZE 1352708
#define INPUT_FRAME_SIZE 2
// extern volatile unsigned int config_frame[CONFIG_FRAME_SIZE]; // 配置数据
// extern unsigned int config_frame2[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame3[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame4[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame5[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame6[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame7[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame8[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame9[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame10[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame11[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame12[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame13[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame14[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame15[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame16[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame17[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame18[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame19[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame20[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame21[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame22[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame23[CONFIG_FRAME_SIZE];  // 配置数据
// extern unsigned int config_frame24[CONFIG_FRAME_SIZE];  // 配置数据
uint32_t config_frame_size = CONFIG_FRAME_SIZE;    // 配置数据大小（32位字数）
// extern unsigned int input_frame[INPUT_FRAME_SIZE]; // 输入数据
uint32_t input_frame_size = INPUT_FRAME_SIZE;      // 输入数据大小（32位字数）

// ================== 辅助函数 ==================
// 获取时间戳函数
static inline uint64_t get_timestamp(void)
{
    uint32_t hi, lo, hi2;
    // 循环读取直到高32位在两次读取之间保持一致
    do
    {
        asm volatile("csrr %0, mcycleh" : "=r"(hi));
        asm volatile("csrr %0, mcycle" : "=r"(lo));
        asm volatile("csrr %0, mcycleh" : "=r"(hi2));
    } while (hi != hi2); // 确保高32位在读取期间没有变化
    return ((uint64_t)hi << 32) | lo;
}

// 打印64位二进制（连续64位，不加分隔）
static void print_binary_64_continuous(uint32_t high, uint32_t low)
{
    // 打印高32位
    for (int i = 31; i >= 0; i--)
    {
        printf("%u", (high >> i) & 1);
    }

    // 打印低32位
    for (int i = 31; i >= 0; i--)
    {
        printf("%u", (low >> i) & 1);
    }
}

// 字符串转换为64位无符号整数（支持二进制和十六进制）
static int string_to_uint64(const char *str, uint32_t *high, uint32_t *low)
{
    char *endptr;

    // 检查前缀
    if (strncmp(str, "0b", 2) == 0 || strncmp(str, "0B", 2) == 0)
    {
        // 二进制格式（64位二进制字符串，例如0b1101...）
        *high = 0;
        *low = 0;
        int bit_count = 0;

        for (int i = 2; str[i] != '\0'; i++)
        {
            if (str[i] == '0' || str[i] == '1')
            {
                if (bit_count < 32)
                {
                    *low = (*low << 1) | (str[i] - '0');
                }
                else if (bit_count < 64)
                {
                    *high = (*high << 1) | (str[i] - '0');
                }
                else
                {
                    return -1; // 超过64位
                }
                bit_count++;
            }
            else if (str[i] == ' ' || str[i] == '_')
            {
                // 跳过空格和下划线
                continue;
            }
            else
            {
                return -1; // 非法字符
            }
        }

        // 如果输入少于64位，高位补零
        if (bit_count < 64)
        {
            // 将低位数据向右移动，使高位对齐
            if (bit_count > 32)
            {
                *high = (*high << (64 - bit_count)) | (*low >> (bit_count - 32));
                *low = *low << (64 - bit_count) >> (64 - bit_count);
            }
            else
            {
                *low = *low << (32 - bit_count);
            }
        }
        return 0;
    }
    else if (strncmp(str, "0x", 2) == 0 || strncmp(str, "0X", 2) == 0)
    {
        // 十六进制格式（最多16个字符，例如0x123456789ABCDEF0）
        unsigned long long value = strtoull(str, &endptr, 16);

        if (*endptr != '\0' && *endptr != ' ')
        {
            return -1; // 非法字符
        }

        *high = (uint32_t)(value >> 32);
        *low = (uint32_t)(value & 0xFFFFFFFF);
        return 0;
    }
    else
    {
        // 十进制格式
        unsigned long long value = strtoull(str, &endptr, 10);

        if (*endptr != '\0' && *endptr != ' ')
        {
            return -1; // 非法字符
        }

        *high = (uint32_t)(value >> 32);
        *low = (uint32_t)(value & 0xFFFFFFFF);
        return 0;
    }
}

// ================== UART接收功能函数 ==================

// 初始化UART接收缓冲区
static void uart_rx_buffer_init(void)
{
    uart_rx_buf.head = 0;
    uart_rx_buf.tail = 0;
    uart_rx_buf.count = 0;
}

// 向缓冲区写入一个字节（中断中调用）
static int uart_rx_buffer_put(uint8_t data)
{
    uint16_t next_head = (uart_rx_buf.head + 1) % UART_RX_BUFFER_SIZE;

    // 检查缓冲区是否已满
    if (next_head == uart_rx_buf.tail)
    {
        return -1; // 缓冲区满，丢弃数据
    }

    uart_rx_buf.buffer[uart_rx_buf.head] = data;
    uart_rx_buf.head = next_head;
    uart_rx_buf.count++;

    return 0;
}

// 从缓冲区读取一个字节（主循环中调用）
static int uart_rx_buffer_get(uint8_t *data)
{
    if (uart_rx_buf.head == uart_rx_buf.tail)
    {
        return -1; // 缓冲区空
    }

    *data = uart_rx_buf.buffer[uart_rx_buf.tail];
    uart_rx_buf.tail = (uart_rx_buf.tail + 1) % UART_RX_BUFFER_SIZE;
    uart_rx_buf.count--;

    return 0;
}

// 获取缓冲区中可用字节数
static uint16_t uart_rx_buffer_available(void)
{
    return uart_rx_buf.count;
}

// 清空接收缓冲区
static void uart_rx_buffer_clear(void)
{
    uart_rx_buf.head = uart_rx_buf.tail;
    uart_rx_buf.count = 0;
}

// ================== UART0中断处理函数 ==================
void UART0_IRQHandler(void)
{
    SAVE_IRQ_CSR_CONTEXT();

    // 检查接收中断状态
    uint32_t status = UART0->IP;

    if (status & UART_IP_RXWM)
    {
        // 读取所有可用的数据
        uint32_t rxfifo_val;
        do
        {
            rxfifo_val = UART0->RXFIFO;
            if (!(rxfifo_val & UART_RXFIFO_EMPTY))
            {
                uint8_t data = (uint8_t)(rxfifo_val & 0xFF);
                uart_rx_buffer_put(data);
            }
        } while (!(rxfifo_val & UART_RXFIFO_EMPTY));
    }

    RESTORE_IRQ_CSR_CONTEXT();
}

// ================== 定时器中断处理函数 ==================
void SysTimer_IRQHandler(void)
{
    SAVE_IRQ_CSR_CONTEXT();

    // 设置超时标志
    snn_timeout_flag = 1;

    // 根据数据手册，当mtime >= mtimecmp时触发中断
    MTIMECMP_REG = 0xFFFFFFFFFFFFFFFFULL;

    // 调试输出
    printf("[TIMER] Timeout interrupt triggered, timeout flag set\n");

    RESTORE_IRQ_CSR_CONTEXT();
}

// ================== SNN中断处理函数 ==================
void SNN_IRQHandler(void)
{
    end_compute_time = get_timestamp();
    printf("\n[SNN] ENTER INTERRUPT\n");
    SAVE_IRQ_CSR_CONTEXT();

    // 清除中断源
    *snn_irq_reg = 0;
    *snn_clilintie = 0;
    int read_finish = 0;

    // 读取64位数据并存储
    data_count = 0;
    finish_count = 0;
    data_buffer[0].high = 0;
    data_buffer[0].low = 0;

    while (read_finish == 0)
    {
        data_buffer[0].high = *(mem_up_base);
        data_buffer[0].low = *(mem_up_base);
        data_count++;

        // 完成帧退出中断
        if (data_packet_number == 0)
        {
            if (((data_buffer[0].high >> 28) & 0xF) == 0xE)
            {
                finish_count++;
                if (finish_count == 1)
                {
                    read_finish = 1;
                }
            }
        }

        // 测试帧退出中断
        if (data_count == data_packet_number)
        {
            read_finish = 1;
            data_packet_number = 0;
        }

        // 直接输出64位二进制
        printf("[SNN] Data[%u]: ", data_count);
        print_binary_64_continuous(data_buffer[0].high, data_buffer[0].low);
        printf(" \n");
    }

    data_ready = 1;
    printf("[SNN] Read %d 64-bit data words\n", data_count);

    RESTORE_IRQ_CSR_CONTEXT();
    printf("[SNN] EXIT INTERRUPT\n");

    // 重新使能SNN中断
    *snn_clilintie = 1;
}

// ================== 命令处理函数 ==================

// 非阻塞方式读取一行命令
static int uart_read_command_noblock(char *buffer, int buffer_size)
{
    static char cmd_buf[MAX_CMD_LENGTH] = {0};
    static int cmd_idx = 0;

    uint8_t data;
    while (uart_rx_buffer_get(&data) == 0)
    {
        // 处理控制字符
        if (data == '\r' || data == '\n')
        {
            if (cmd_idx > 0)
            { // 有命令需要处理
                cmd_buf[cmd_idx] = '\0';

                // 复制到输出缓冲区
                strncpy(buffer, cmd_buf, buffer_size - 1);
                buffer[buffer_size - 1] = '\0';

                // 重置索引
                int result = cmd_idx;
                cmd_idx = 0;

                // 回显换行
                printf(" \n");

                return result;
            }
        }
        // 处理退格键和删除键
        else if (data == '\b' || data == 0x7F || data == 0x08)
        {
            if (cmd_idx > 0)
            {
                cmd_idx--;
                printf("\b \b"); // 回显退格效果
            }
        }
        // 处理Ctrl+C（中断）
        else if (data == 0x03)
        {
            printf("^C\n> ");
            cmd_idx = 0;
        }
        // 可打印字符
        else if (data >= 32 && data <= 126)
        {
            if (cmd_idx < MAX_CMD_LENGTH - 1)
            {
                cmd_buf[cmd_idx++] = data;
                printf("%c", data); // 回显字符
            }
        }
    }

    return -1; // 没有完整命令
}

// 写入64位数据到FIFO
static void write_64bit_to_fifo(uint32_t high, uint32_t low)
{
    *(mem_down_base) = high;
    *(mem_down_base) = low;
}

// ================== 命令执行函数 ==================

// 显示帮助信息
static void cmd_help(void)
{
    printf("\n===== Available Commands =====\n");
    printf("help                     - Show this help message\n");
    printf("status                   - Show system status\n");
    printf("clear                    - Clear UART buffer\n");
    printf("\nSNN Configuration:\n");
    printf("snn config               - Send configuration data to SNN\n");
    printf("snn input                - Send predefined input_frame to SNN\n");
    printf("snn status               - Show SNN status\n");
    printf("snn test                 - Test SNN data output format\n");
    printf("\nUser Input:\n");
    printf("user <64-bit-value>      - Send 64-bit value directly to SNN\n");
    printf("user help                - Show user input examples\n");
    printf("================================\n");
}

// 显示用户输入帮助
static void cmd_user_help(void)
{
    printf("\n===== User Input Examples =====\n");
    printf("Send a 64-bit value directly to SNN:\n");
    printf("  user 0x123456789ABCDEF0\n");
    printf("  user 0b1010101010101010101010101010101010101010101010101010101010101010\n");
    printf("  user 12297829382473034410\n");
    printf("\nNote: The value will be sent immediately to SNN.\n");
    printf("================================\n");
}

// 显示系统状态
static void cmd_status(void)
{
    printf("\n===== System Status =====\n");
    printf("CPU Frequency: %lu Hz\n", SystemCoreClock);
    printf("UART Baudrate: 3000000 bps\n");
    printf("UART RX Buffer: %d/%d bytes\n",
           uart_rx_buffer_available(), UART_RX_BUFFER_SIZE);
    printf("SNN Data Ready: %s\n", data_ready ? "YES" : "NO");
    printf("SNN Data Count: %d\n", data_count);
    printf("SNN IRQ Enabled: %s\n", (*snn_clilintie) ? "YES" : "NO");
    printf("Config Frame Size: %u 32-bit words\n", config_frame_size);
    printf("Input Frame Size: %u 32-bit words\n", input_frame_size);
    printf("===========================\n");
}

// 发送配置数据到SNN
static void cmd_snn_config(void)
{
    printf("\nSending configuration data to SNN...\n");

    // 禁用SNN中断，防止在配置过程中触发
    *snn_clilintie = 0;

    // config_frame包含可变数量的32位字
    if (config_frame == NULL || config_frame_size == 0)
    {
        printf("ERROR: Config frame not defined!\n");
        return;
    }

    uint32_t total_64bit = config_frame_size / 2;
    printf("Total 64-bit data to send: %d\n", total_64bit);

    start_time = get_timestamp();
    for (uint32_t i = 0; i < config_frame_size; i += 2)
    {
        // 确保不越界
        write_64bit_to_fifo(config_frame[i + 1], config_frame[i]);

        // printf("[SNN] Data[%u]: ", i);
        // print_binary_64_continuous(config_frame[i], config_frame[i+1]);
        // printf(" \n");

        // 每发送1000个数据包显示一次进度
        if ((i / 2) % 1000 == 0 && i / 2 > 0)
        {
            printf("Progress: %u/%u\n", i / 2, total_64bit);
        }
    }

    end_time = get_timestamp();

    printf("Configuration data sent successfully.\n");

    printf("Config Time: %.2f cycles\n", (double)(end_time - start_time));

    // 重新使能SNN中断
    *snn_clilintie = 1;
}

// 发送预定义的input_frame数据到SNN
static void cmd_snn_input(void)
{
    printf("\nSending predefined input_frame to SNN...\n");

    // 禁用SNN中断，防止在配置过程中触发
    *snn_clilintie = 0;

    // 检查input_frame是否定义
    if (input_frame == NULL || input_frame_size == 0)
    {
        printf("ERROR: Input frame not defined!\n");
        // 重新使能SNN中断
        *snn_clilintie = 1;
        return;
    }

    uint32_t total_64bit = input_frame_size / 2;
    printf("Total 64-bit data to send: %d\n", total_64bit);

    // input_frame按64位发送
    for (uint32_t i = 0; i < input_frame_size; i += 2)
    {
        // 确保不越界
        write_64bit_to_fifo(input_frame[i + 1], input_frame[i]);

        // 每发送1000个数据包显示一次进度
        if ((i / 2) % 1000 == 0 && i / 2 > 0)
        {
            printf("Progress: %u/%u\n", i / 2, total_64bit);
        }
    }

    printf("input data sent successfully.\n");

    // 重新使能SNN中断
    *snn_clilintie = 1;
}

// 测试SNN数据输出格式
static void cmd_snn_test(void)
{
}

// 处理用户输入命令 - 直接发送64位数据
static void cmd_user_input(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: user <64-bit-value>\n");
        printf("Examples:\n");
        printf("  user 0x123456789ABCDEF0\n");
        printf("  user 0b1010101010101010101010101010101010101010101010101010101010101010\n");
        printf("  user 12297829382473034410\n");
        printf("\nFor more examples, type 'user help'\n");
        return;
    }

    // 检查是否请求帮助
    if (strcmp(argv[1], "help") == 0)
    {
        cmd_user_help();
        return;
    }

    uint32_t temp_high, temp_low;

    // 尝试解析64位值
    if (string_to_uint64(argv[1], &temp_high, &temp_low) != 0)
    {
        printf("ERROR: Invalid format for 64-bit value: %s\n", argv[1]);
        printf("Valid formats:\n");
        printf("  Binary: 0b1010...\n");
        printf("  Hex: 0x123456789ABCDEF0\n");
        printf("  Decimal: 12345678901234567890\n");
        return;
    }

    uint8_t highest_two_bits = (temp_low >> 30) & 0x03;
    uint16_t low_14bits = temp_high & 0x3FFF;

    // 检查最高2位
    if (data_packet_number == 0)
    {
        if (highest_two_bits == 0x00)
        {
            printf("  Detected config frame\n");
            data_packet_number = 1;
        }
        else if (highest_two_bits == 0x01)
        {
            printf("  Detected test frame \n");
            data_packet_number = low_14bits + 1;
        }
        else if (highest_two_bits == 0x02)
        {
            printf("  Detected work frame\n");
        }
        else if (highest_two_bits == 0x03)
        {
            printf("  Detected control frame\n");
        }
    }

    printf("\nSending 64-bit user input to SNN...\n");

    // 禁用SNN中断，防止在配置过程中触发
    *snn_clilintie = 0;

    start_compute_time = get_timestamp();

    // 直接发送到FIFO
    write_64bit_to_fifo(temp_high, temp_low);

    // printf("64-bit user input sent successfully.\n");

    // 重新使能SNN中断
    *snn_clilintie = 1;
}

// 解析命令行参数
static int parse_command_line(const char *cmd_line, char *argv[], int max_args)
{
    char cmd_copy[MAX_CMD_LENGTH];
    strncpy(cmd_copy, cmd_line, MAX_CMD_LENGTH - 1);
    cmd_copy[MAX_CMD_LENGTH - 1] = '\0';

    int argc = 0;
    char *token = strtok(cmd_copy, " ");

    while (token != NULL && argc < max_args)
    {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    return argc;
}

// 处理接收到的命令
static void process_command(const char *cmd)
{
    if (strlen(cmd) == 0)
        return;

    // 解析命令行
    char *argv[10];
    int argc = parse_command_line(cmd, argv, 10);

    if (argc == 0)
        return;

    // 主命令
    if (strcmp(argv[0], "help") == 0)
    {
        cmd_help();
    }
    else if (strcmp(argv[0], "status") == 0)
    {
        cmd_status();
    }
    else if (strcmp(argv[0], "clear") == 0)
    {
        uart_rx_buffer_clear();
        printf("UART buffer cleared.\n");
    }
    else if (strcmp(argv[0], "snn") == 0 && argc > 1)
    {
        // SNN子命令
        if (strcmp(argv[1], "config") == 0)
        {
            cmd_snn_config();
        }
        else if (strcmp(argv[1], "input") == 0)
        {
            cmd_snn_input();
        }
        else if (strcmp(argv[1], "status") == 0)
        {
            printf("\nSNN Status:\n");
            printf("  Data Ready: %s\n", data_ready ? "YES" : "NO");
            printf("  Data Count: %d\n", data_count);
            printf("  IRQ Enabled: %s\n", (*snn_clilintie) ? "YES" : "NO");
        }
        else if (strcmp(argv[1], "test") == 0)
        {
            cmd_snn_test();
        }
        else
        {
            printf("Unknown SNN command: '%s'\n", argv[1]);
        }
    }
    else if (strcmp(argv[0], "user") == 0)
    {
        // 用户输入命令
        cmd_user_input(argc, argv);
    }
    else
    {
        printf("Unknown command: '%s'\n", cmd);
        printf("Type 'help' for available commands.\n");
    }
}

// ================== 主程序 ==================
int main(void)
{
    unsigned long hartid, clusterid;
    uint32_t returnCode;

    SPI_Init();

    // 获取硬件信息
    hartid = __get_hart_id();
    clusterid = __get_cluster_id();

    // 初始化CSR
    __RV_CSR_SET(CSR_MSTATUS, MSTATUS_XS);
    printf(" \n");
    printf("===============================================\n");
    printf("    RISC-V SNN System with UART Interface\n");
    printf("===============================================\n");
    printf("System initialized by system_evalsoc.c\n");
    printf("Hardware Info:\n");
    printf("  Hart ID: %lu, Cluster ID: %lu\n", hartid, clusterid);
    printf("  CPU Frequency: %lu Hz\n", SystemCoreClock);
    printf("  UART Baudrate: 3000000 bps\n");
    printf("===============================================\n\n");

    // 注意：UART已经在system_evalsoc.c的_premain_init()中初始化
    // 波特率已设置为3000000

    // 初始化UART接收缓冲区
    uart_rx_buffer_init();

    // 配置UART0中断（接收水位和使能中断）
    uart_set_rx_watermark(UART0, UART_FIFO_WATERMARK);
    uart_enable_rxint(UART0);

    // 注册UART0中断处理函数
    returnCode = ECLIC_Register_IRQ(
        UART0_IRQn,
        ECLIC_NON_VECTOR_INTERRUPT,
        ECLIC_LEVEL_TRIGGER,
        2, // 优先级：2（较低优先级，不干扰SNN）
        0, // 不使用中断向量
        UART0_IRQHandler);

    if (returnCode != 0)
    {
        printf("ERROR: Failed to register UART0 interrupt (code: %d)\n", returnCode);
    }
    else
    {
        printf("UART0 interrupt registered successfully.\n");
    }

    // 注册SNN中断处理函数
    returnCode = ECLIC_Register_IRQ(
        SNN_IRQn,
        ECLIC_NON_VECTOR_INTERRUPT,
        ECLIC_LEVEL_TRIGGER,
        1, // 优先级：1（较高优先级）
        0,
        SNN_IRQHandler);

    if (returnCode != 0)
    {
        printf("ERROR: Failed to register SNN interrupt (code: %d)\n", returnCode);
    }
    else
    {
        printf("SNN interrupt registered successfully.\n");
    }

    // 注册系统定时器中断处理函数
    returnCode = ECLIC_Register_IRQ(
        SysTimer_IRQn,
        ECLIC_NON_VECTOR_INTERRUPT,
        ECLIC_LEVEL_TRIGGER,
        0, // 优先级：0（最高优先级，可以打断SNN中断）
        0,
        SysTimer_IRQHandler);

    if (returnCode != 0)
    {
        printf("ERROR: Failed to register SysTimer interrupt (code: %d)\n", returnCode);
    }
    else
    {
        printf("SysTimer interrupt registered successfully.\n");
    }

    // 使能全局中断
    __enable_irq();

    // 初始化状态变量
    data_ready = 0;
    data_count = 0;
    data_packet_number = 0;
    snn_timeout_flag = 0;
    MTIMECMP_REG = 0xFFFFFFFFFFFFFFFFULL;

    printf("System ready.\n");
    printf("\nImportant:\n");
    printf("  - Use 'help' for available commands.\n");
    printf("  - Use 'snn config' to send predefined config frame\n");
    printf("  - Use 'snn input' to send predefined input frame\n");
    printf("  - Use 'user <64-bit-value>' to send direct user data\n");
    printf("> ");

    // 主循环
    while (1)
    {
        // ============ 1. 处理UART命令（非阻塞） ============
        char command_buffer[MAX_CMD_LENGTH];
        int cmd_len = uart_read_command_noblock(command_buffer, sizeof(command_buffer));

        if (cmd_len > 0)
        {
            // 执行命令
            process_command(command_buffer);

            // 显示新的提示符
            printf("> ");
        }

        // ============ 2. 处理SNN数据 ============
        if (data_ready)
        {
            printf("\n[SNN] Processing received data...\n");

            // 这里可以添加SNN数据处理逻辑
            // 例如：分析、存储、转发等

            data_ready = 0;
            printf("[SNN] Data processing complete.\n");

            printf("Thread Compute Time: %.2f cycles\n", (double)(end_compute_time - start_compute_time));

            // 显示提示符（如果命令行被中断）
            printf("> ");
        }

        // ============ 3. 其他后台任务 ============
    }

    return 0;
}
