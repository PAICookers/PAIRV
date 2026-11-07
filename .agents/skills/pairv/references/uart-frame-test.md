# UART 帧验证

目标是已跟踪的 `application/baremetal/uart/`，先核对应用 `Makefile`、`include/uart_frame.h` 与命令实现。此工作流用于命令 shell，二进制 relay 会话读[Relay 验证](relay-test.md)。每次恢复后从 CONFIG 开始，成功后才 INIT；WORK 用 SYNC 收尾并等 COMPLETE，不重叠尚未完成的控制事务。

## 准备外部帧

将用户提供的目录设为 `FRAME_DIR`，包含固定名称的 `config_frame.h`、`init_frame.h`、`work_frame.h`、`sync_frame.h`，以及可选 `test_frame.h`。

每个启用的头直接定义对应 `HAS_CONFIG_FRAME` / `HAS_INIT_FRAME` / `HAS_WORK_FRAME` /`HAS_SYNC_FRAME` / `HAS_TEST_FRAME` 宏和 `config_frame` / `init_frame` / `work_frame` /`sync_frame` / `test_frame` 数组。元素类型为 uint32_t，按导出数组的逻辑 `high32, low32` 成对排列；实际 FIFO 顺序由 driver 处理，不自行交换模型数据。大型只读数组使用 `.large_const_data` 段。

数组须在包含它的编译单元中可用 `sizeof`，不能只有 `extern` 声明；不把头加入 `C_SRCS`。缺少头或宏的帧不可用，命令应报告 `unavailable`；奇数元素等错误按当前帧检查拒绝。不要把临时模型目录当成仓库随附资产。

## 构建与交互

按 [构建与下载](common/build-and-flash.md) 确认环境与参数，从仓库根构建：

```bash
make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/uart FRAME_DIR="$FRAME_DIR" clean all
```

不传 `FRAME_DIR` 时使用应用内已跟踪的默认帧。外部路径按当前工具环境解析；核对实际 ELF hash，避免多模型构建相互覆盖。仅编译时不访问硬件；需要下载/验证时使用同一参数，按 [串口操作](common/serial-capture.md) 先捕获 banner。

基本命令顺序：

```text
snn config
snn init
snn work
snn sync
status
```

`snn input` 是 work 别名，不是额外必做一步。`snn test` 按需执行；无 test 帧时安全报告 unavailable，有帧时按源码 TEST header 的响应计数结束。重复 INIT 必须等前次完成；WORK 尚有响应时 INIT 应拒绝为 `response pending`，可用 SYNC 完成本次响应。

默认 CONFIG 发送检查为5秒、WORK响应3秒、其他控制响应2秒，执行前仍核对当前源码。CONFIG 计时检查发生在 NoC 写返回后，不能中断阻塞 MMIO。记录每次命令是否回到 shell、COMPLETE、原始响应和错误；不由该流程推断模型数值正确。
