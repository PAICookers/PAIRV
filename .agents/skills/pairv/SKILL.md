---
name: pairv
description: 开发、构建和验证 PAIRV RISC-V 固件与 C runtime，联调 relay/UART，按指定 tag 更新 NMSIS/FlatBuffers，并查询 SDK、板卡与芯片资料。不负责模型编译或 Host Python runtime 开发。
---

# PAIRV 项目工作流

从当前 checkout 定位仓库根，以当前源码和已跟踪文档为准。按下表只读取当前任务的工作流；组合任务按需串联，不预读全部文件。

| 任务 | 读取 |
| --- | --- |
| 固件编译、下载、启动或板卡排错 | [构建与板卡运行](references/board-build-flash.md) |
| `Lib/runtime`、`rvrt_*` API 或 C 调用方修改/审阅 | [C runtime 开发](references/runtime-development.md) |
| 二进制 Host ↔ relay ↔ NoC 配置与推理验证 | [Relay 验证](references/relay-test.md) |
| `application/baremetal/uart` 命令与模型帧测试 | [UART 帧验证](references/uart-frame-test.md) |
| NMSIS 升级或升级流程审阅 | [NMSIS 更新](references/update-nmsis.md) |
| FlatBuffers 升级或升级流程审阅 | [FlatBuffers 更新](references/update-flatbuffers.md) |

查询 SDK、器件手册、CPU/NICE 设计或维护资料索引时，读[资料查询](references/reference-index.md)。纯构建或代码任务不必加载资料目录。

## 公共操作：用到时再读

- Windows/Linux 环境与基础开发操作：[项目 README](../../../README.md)。
- 重建与下载参数、固件身份检查：[构建与下载](references/common/build-and-flash.md)。
- 串口采集、启动与实板证据：[串口操作](references/common/serial-capture.md)。

保留用户修改。仅编译不访问板卡，普通构建或流程审阅不更新依赖；实际升级需用户明确要求并指定发布 tag。下载会立即启动；需验证运行时，先打开串口且只保留一个读取器。分别报告 Host、交叉构建、协议与数值证据。
