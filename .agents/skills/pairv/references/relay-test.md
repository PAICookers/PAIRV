# Relay 验证

先读已跟踪的 [relay 说明](../../../../application/baremetal/relay/README.md)、应用 `Makefile` 与 `relay_protocol.h`。正常接入沿用 README；只验证协议编码时执行 `host-test`，不访问板卡。工具准备见 [项目 README](../../../../README.md)；需要下载或采集时，再读 [构建与下载](common/build-and-flash.md)、[串口操作](common/serial-capture.md)。

按任务选择正常协议或错误路径，不因调用技能自动进行故障注入。两种配置来源均由 Host SETUP 触发；Host 模式下 RV 仍负责 NoC 转发。

## 正常事务

- 固件 `ENABLE_HOST_CONFIG` 匹配 Host 配置来源；`RELAY_HOST_WORD_ORDER=high|low` 匹配应用字序。构建/上传使用相同参数，源码或宏变化后 `clean all`。
- 静态配置头须直接定义 `HAS_CONFIG_FRAME` 与成对逻辑 high32/low32 数组。按 `CONFIG_FRAME_FILE` / `FRAME_DIR` 当前优先级选择，不能引用某台机器上的默认模型。
- 启动前打开稳定串口并捕获 banner；先消费 `relay host word order: ...` 及 SDK 文本，再开始二进制会话。每帧8字节，32-bit字内部大端。
- `F0 SETUP`、`F1 ACK` 和配置前 STATUS 固定 high-first；Host payload 从一开始按应用字序发送，配置后推理与 `F2 STATUS` 也按应用字序。
- SETUP 数量为1..0xFFFFF个8字节帧；CRC可选，按逻辑high32/low32计算 CRC-32/ISO-HDLC。控制opcode是8bit，不能当作全局保留的芯片4bit帧头。
- Host 来源发送全部配置payload；静态来源不发payload。核对ACK错误码、实际数量与CRC，再执行 INIT/COMPLETE，以及每步 WORK/SYNC(1)/COMPLETE。
- 使用 Host SDK 时保留配置传输预算、单个绝对deadline及分块间提前失败检查。Host发完不证明RV已转发；ACK也不包含芯片读回校验。

协议字段以 `application/baremetal/relay/relay_protocol.h` / `.c` 为准；不在技能里复制另一份二进制布局。

## 按需错误验收

错误注入必须在独立、已恢复的实验中进行，保留原输入/固件和全部RX字节。正常来源/字序/CRC组合与错误路径分别报告，不把通用回归当成实板结果。

| 检查 | 预期与边界 |
| --- | --- |
| 非法字段/来源/字序/静态数量 | 转发前失败ACK，count/CRC为0；relay继续等待新SETUP。Host SDK已FAULTED时不能复用该session |
| CRC错误 | 实际转发后失败ACK，报告数量/CRC；relay终止，须恢复后新建session |
| UART ring溢出 | 一次STATUS5，关闭RX/NoC中断并终止；不继续消费残留流 |
| SYNC路径检测到NoC读取错误 | STATUS6并终止；NoC标志注入仅证明报告/状态路径，不证明真实MMIO失败检测 |
| 超时 | 保存部分TX/RX与启动证据，不能只凭timeout判断RV/芯片进度或根因 |

事务错误不能先输出UART文本；SDK退出尾字节单独保留。故障后确认Host拒绝后续I/O，并按协议实际支持的方式检查relay终态；不要为“探活”续发芯片输入。

NoC MMIO无软件超时保护。重启RV或打开新session不能证明芯片/桥已复位；按板卡流程恢复两侧状态，必要时完整断电。恢复成功不等于已定位硬件根因。VOLTAGE lane完整性与有独立oracle的数值比较分开记录。
