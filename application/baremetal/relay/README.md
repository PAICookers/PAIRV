# PAICORE relay

relay 运行在 RV CPU 上，把 Host 的 UART 帧转发到 PAICORE NoC，并把芯片响应传回
Host。模型编译、输入预处理和结果解释由应用负责。

## 选择配置来源

| 模式 | RV 构建参数 | Host `RuntimeSession` 参数 | 配置帧来源 |
| --- | --- | --- | --- |
| Host 配置 | `ENABLE_HOST_CONFIG=1` | `enable_host_config=True` | Host 读取 `.pb`，经 UART 发送；RV 接收并转发到 NoC |
| 静态配置（默认） | `ENABLE_HOST_CONFIG=0` | `enable_host_config=False` | RV 转发固件内的 `config_frame[]`；Host 仍使用配套 `.pb` 的映射 |

**两种模式都等待 Host 的 `CONFIG_SETUP`，不会在 RV 启动时自动配置芯片。**
Host 声明的来源必须与固件一致。Host 配置模式不包含静态配置头，但仍需要 RV 完成
UART 到 NoC 的转发。

## 构建并启动 RV 固件

以下命令从 PAIRV 根目录执行。先按[项目说明](../../../README.md#quick-start)设置
Nuclei 工具链，再构建 Host 配置、high-first 字序的调试固件：

```bash
export NUCLEI_SDK_ROOT="$PWD"
source ./setup.sh
make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/relay \
  ENABLE_HOST_CONFIG=1 RELAY_HOST_WORD_ORDER=high \
  RV_DEBUG_ENABLE_LOGGING=1 clean all
sha256sum application/baremetal/relay/relay.elf
```

relay 只支持 `DOWNLOAD=ilmflashxip` 或 `flashxip`。切换配置来源、配置头、字序或日志
开关后，重新执行 `clean all`；`upload` 使用同一组参数。

上传前先打开连接板卡的稳定 UART 并保存启动日志，默认波特率为 **3000000**。
Linux 可通过 `/dev/serial/by-id/` 核对设备身份，不依赖可能变化的 `ttyUSB` 编号。
将 `PAIRV_OPENOCD` 设置为[定制 OpenOCD](../../../README.md#paicore-board-programming)
的可执行文件路径，然后上传：

```bash
make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/relay \
  ENABLE_HOST_CONFIG=1 RELAY_HOST_WORD_ORDER=high \
  RV_DEBUG_ENABLE_LOGGING=1 OPENOCD="$PAIRV_OPENOCD" upload
```

`upload` 会下载并立即启动程序。确认日志出现
`relay host word order: High first`；low-first 固件对应 `Low first`。
先消费完启动文本，再停止串口监视器，由 Host 应用独占 UART 执行二进制事务。

### 使用静态配置

将 `CONFIG_FRAME_HEADER` 设置为配套模型配置头的路径，再改用下列参数构建；上传时也保留相同参数：

```bash
make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/relay \
  ENABLE_HOST_CONFIG=0 CONFIG_FRAME_FILE="$CONFIG_FRAME_HEADER" \
  RELAY_HOST_WORD_ORDER=high RV_DEBUG_ENABLE_LOGGING=1 clean all
```

配置头必须直接定义 `HAS_CONFIG_FRAME` 和 `config_frame[]`，每个逻辑帧按
`high32, low32` 两个 `uint32_t` 元素排列，数组放在 `.large_const_data` 段。
使用配套模型导出的配置头；不要把它加入 `C_SRCS`，也不要仅提供 `extern` 声明。
缺少文件或宏、数组为空或元素数为奇数都会使构建失败。

`CONFIG_FRAME_FILE` 优先于 `FRAME_DIR`；后者默认选择目录中的 `config_frame.h`。
相对路径从 relay 应用目录解析，外部配置头建议通过变量传入完整路径。

## Host 接入与通信顺序

Host 使用配套 `.pb` 和完整时间轴的输入。Host runtime 仓库提供
`examples/run_sample.py` 单输入示例；多输入、DATA/VOLTAGE 解码和生命周期见其
`docs/usage.md`。两者需要在 Host runtime 环境中使用，不由 RV 固件执行。

1. Host 发送 `CONFIG_SETUP`，声明来源、8 字节配置帧数量、字序和 CRC 开关。
2. Host 模式由 Host 发送配置 payload；静态模式由 RV 读取内置数组。
   RV 全部转发后返回 `CONFIG_ACK`，Host 校验错误码、数量和可选 CRC。
3. ACK 成功后执行 `INIT -> COMPLETE`，再逐 timestep 执行
   `WORK -> SYNC(1) -> 输出/COMPLETE`，收齐响应后继续下一步。

ACK 只确认 RV 已完成声明数量的 NoC 转发调用及可选 CRC 校验，不包含芯片配置读回
或模型数值验证。Host 的配置 deadline 包含传输预算和额外 `timeout`；单纯延长等待
不能恢复已经阻塞或丢失数据的事务。

### 字序与控制帧

- `RELAY_HOST_WORD_ORDER=high|low` 必须匹配 Host 的 `host_word_order`，默认 `high`。
  high-first 先发送高 32 位，low-first 先发送低 32 位；每个 32 位字内部按大端发送。
- `CONFIG_SETUP`、`CONFIG_ACK` 和配置成功前的 `RELAY_STATUS` 固定 high-first。
  配置 payload 从一开始就使用声明的应用字序；ACK 成功后推理和状态帧也使用该字序。
- 配置数量是 `1..0xFFFFF` 个 **8 字节帧**，不是字节数或 32 位字数。
  CRC 默认关闭，采用 CRC-32/ISO-HDLC，按逻辑 high32/low32 字节计算，不受 UART 字序影响。
- `0xF0 SETUP`、`0xF1 ACK`、`0xF2 STATUS` 是 relay 的 8-bit opcode，不能等同于
  PAICORE 的 4-bit 帧头。配置 payload 保持 opaque，不把其中的相似前缀当作控制指令。

## 主机自测

从 PAIRV 根目录执行应用专用目标；根 Makefile 不转发 `host-test`：

```bash
make -C application/baremetal/relay DOWNLOAD=ilmflashxip \
  ENABLE_HOST_CONFIG=1 host-test
```

成功时输出 `relay protocol host tests passed`。它覆盖 SETUP 字段、ACK/STATUS 编码和
CRC 已知向量，不执行 UART 中断或 NoC 硬件访问。真实板卡还需验证启动日志、配置 ACK、
INIT/SYNC 的 COMPLETE，以及有独立参考的模型输出；仅构建或上传成功不算推理通过。

## 错误与恢复

| 情况 | relay 行为 | 恢复方式 |
| --- | --- | --- |
| 配置字段、来源、字序或静态数量不匹配 | 配置转发前返回失败 ACK，数量和 CRC 为 0，继续等待完整 SETUP | 低层工具可修正后重发；Host session 已进入 FAULTED 时需新建 session |
| 配置已经转发后的 CRC 错误 | 返回失败 ACK 并终止 relay | 复位恢复后，以新 session 重新配置 |
| UART RX ring 溢出，或 SYNC 等待路径检测到 NoC 读取失败 | 停止接收与 NoC 中断，丢弃队列/半帧，只上报一次 STATUS 并退出 | 复位恢复后，以新 session 重新配置，不支持断点续传 |
| 配置 ACK 或 COMPLETE 超时 | 尚不能据此判断 RV 和芯片实际进度 | 保存 UART/固件证据，检查板卡链路与启动状态，再恢复会话 |

NoC MMIO 访问没有软件超时保护。重新编程或仅重启 RV 不能证明 PAICORE 芯片和桥接
状态也已复位；持续不响应时需按板卡流程检查两侧供电、时钟、复位与 NoC 链路。
完整断电恢复只能说明状态发生了变化，不能据此认定具体硬件根因。

配置和推理事务期间 UART0 只传二进制协议，错误通过 ACK/STATUS 上报，不混入文本日志。
SDK 可能在程序退出后追加尾字节；保留原始 RX，Host 收到终止错误后停止解析该事务。
