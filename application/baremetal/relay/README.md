# PAICORE relay

该 app 是 UART 到 PAICORE NoC 的透明桥。它接收 Host 的配置和推理帧，转发到
NoC，并返回 `INIT/SYNC/COMPLETE`、DATA/VOLTAGE 输出和错误状态。配置阶段由 Host
显式启动，完成后才进入推理数据流。

## 配置与使用

relay 支持两种配置来源：

- `ENABLE_HOST_CONFIG=0`：从 `CONFIG_FRAME_FILE` 编译进来的静态数组转发配置；
- `ENABLE_HOST_CONFIG=1`：Host 在 setup 后发送配置帧，relay 原样转发。

配置时序分别为：

- 静态模式：`CONFIG_SETUP -> relay 转发静态配置 -> CONFIG_ACK`，Host 不发 payload；
- Host 模式：`CONFIG_SETUP -> Host 发送配置 payload -> CONFIG_ACK`。

Host 声明的配置来源必须与 `ENABLE_HOST_CONFIG` 一致。静态模式必须提供存在的
配置头，直接定义 `HAS_CONFIG_FRAME` 和按 `(high32, low32)` 排列的 `config_frame[]`；
缺失文件或宏会构建失败。Host 模式不包含该头。配置 payload 不解析 `INIT/SYNC`
前缀。配置计数是 20-bit 的 8-byte 帧数，范围为 `1..0xFFFFF`，不是字节数。

应用帧字序由 `RELAY_HOST_WORD_ORDER=high|low` 固定。`CONFIG_SETUP`、`CONFIG_ACK`
和配置成功前的 `RELAY_STATUS` 始终 high-first。Host 配置 payload 从一开始就使用
声明的应用字序；ACK 成功后推理帧和运行状态也使用应用字序。CRC32 可由 setup
开启，默认关闭，按逻辑 high32/low32 字节计算，不受 UART 字序影响。

ACK 成功后先执行 `INIT -> COMPLETE`，再逐 timestep 执行
`WORK -> SYNC(1) -> 输出/COMPLETE`；Host 收齐响应后才继续下一步。

从 PAIRV 根目录构建；每次切换配置来源、帧文件或字序都执行 `clean all`，并在
clean、all、upload 中使用相同参数：

```bash
source ./setup.sh
# 静态配置
make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/relay \
  ENABLE_HOST_CONFIG=0 CONFIG_FRAME_FILE=frames/config_frame.h \
  RELAY_HOST_WORD_ORDER=high RV_DEBUG_ENABLE_LOGGING=1 clean all
# Host 配置
make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/relay \
  ENABLE_HOST_CONFIG=1 RELAY_HOST_WORD_ORDER=high \
  RV_DEBUG_ENABLE_LOGGING=1 clean all
```

上传前按 `0-pairv-board-build-flash` 和 `6-pairv-relay-test` 技能打开稳定串口，
完整捕获启动日志；使用已确认的 OpenOCD，再以当前构建的相同参数执行 `upload`。

`CONFIG_ACK` 携带错误码、已转发数量和 CRC32。配置字段错误发生在任何配置帧转发前，
relay 回复错误（数量和 CRC 均为 0）并继续等待下一次 `CONFIG_SETUP`；低层测试可重试。
`RuntimeSession` 遇到配置事务失败会使 Host session 进入 sticky `FAULTED`，
不能直接在同一 session 重试。UART 溢出、NoC 读取错误或已转发配置后的 CRC 错误会
锁存并退出 relay 主循环；这些错误或 Host 超时后必须复位 relay，以新 session
重新配置。详细字段和错误码见 Host runtime 的 `docs/relay_protocol.md`。

relay 控制 opcode 是 UART 协议的 8-bit 顶层字节：`0xF0 CONFIG_SETUP`、`0xF1
CONFIG_ACK`、`0xF2 RELAY_STATUS`。它们不等同于芯片的 4-bit 帧头；配置 payload
保持 opaque，避免把 payload 中偶然出现的 `F0/F1/F2` 当成控制帧。

UART RX ring buffer 首次溢出后停止接收新字节；主循环优先关闭 RX 和 NoC 中断、丢弃队列和未完成帧，并只上报一次 `UART_RX_OVERFLOW`。等待 SYNC 完成时发生溢出也会退出等待，进入相同失败态。溢出后必须复位 relay，不能在原字节流上继续推理。

配置/推理事务期间 UART0 使用二进制帧协议；事务错误通过 `CONFIG_ACK` 或 `RELAY_STATUS` 上报，不在错误帧前插入文本日志。程序退出后 SDK 可能追加退出尾字节，Host 收到终止错误后停止解析该事务。
