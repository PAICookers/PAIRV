# SNN Head Board Test (board_test)

在真实 **PAICore（N307FD RISC-V CPU + 类脑核）** 硬件上，对完整 SNN Head 进行分层与端到端验证的裸机测试工程。

每个测试为独立可执行 ELF，经完整 PAICore runtime（session / frame_codec / artifact 编解码）向类脑核发送并等待帧，因此仅可在真实硬件上运行。QEMU 仅仿真 N307FD 的 RISC-V CPU，不含类脑核，测试将在 `sync_wait` 处超时（2000 ms）。若仅需在 QEMU 上验证 CPU 侧算子（LayerNorm / 量化 / FC / 反量化），请使用 [`../cpu_ops_selftest`](../cpu_ops_selftest/README.md)。

## 测试项

每个子目录都是一个独立 Make 工程，入口为该目录下的 `main.c`。board test
默认固定启用 `SNN_HEAD_TIMING=1`，输出统一的分阶段耗时报告。

| 目录         | 覆盖范围                                     | 备注                                          |
| ------------ | -------------------------------------------- | --------------------------------------------- |
| `fc1_lif`    | LN1 → fc1 → lif_in                         | 默认值                                        |
| `block0_lif` | block0：LN → fc → LIF                      |                                               |
| `block1_lif` | block1：LN → fc → LIF                      |                                               |
| `fc2`        | LN2 → fc2 → li_out（膜电位）               |                                               |
| `fc3`        | fc3 → action                                | 使用 fc3 artifact                             |
| `chain`      | fc1 → block0 → block1 → fc2 → fc3 端到端 | 五层共享同一 tensor_workspace                 |

## 验证模型

每个 ELF 都在板上对运行结果作逐元素 golden 判定，UART 中出现明确的
`<layer>: GOLDEN PASS ...` 才算通过。

- **单层测试**：向目标层输入对应的 golden 输入。前三层比较 0/1 spike 的精确
  float32 值；`fc2` 和 `fc3` 比较反量化 float32，容差为
  `abs_error <= 1e-5 + 1e-5 * abs(expected)`。
- **端到端测试（`chain`）**：执行 `snn_head_run_chunk()`，五层经同一
  tensor_workspace 接力，最终用相同容差比较 `action[8][7]` 和
  `golden_fc3_out`。每层部署完成后先发送 INIT，使该层的输入、同步和输出时间步
  都从本层 `t=0` 开始。若某层 INIT 无法完成，chain 应直接失败，而不是沿用上一层
  的硬件时间线。
- 首个不匹配会打印元素索引、期望值/实际值的 IEEE754 位型和误差。需要更多上下文时
  使用 `DUMP=1`，它只提供诊断数据，不替代板上判定。

## Golden 数据

`snn_head_golden.c` 及各层边界参考张量由 `gen_snn_head_golden.py` 生成。脚本从
INT8 QAT 导出包重建 CPU 边界算子，并调用与 artifact 相同的 PAIBox lowering 得到
PAICORE LIF 参数，再模拟整数 spike 状态。它不能直接使用 QAT runtime 的精确
`beta_float`，因为部署会把 beta 映射到硬件格点，并调整严格阈值比较。
输出表示与 C 侧 `tensor_workspace` 一致：

```text
snn_head_golden_input      x       float32[8][768]
snn_head_golden_fc1_out    lif_in  float32[8][1536]  (spike 0/1)
snn_head_golden_block0_out block0  float32[8][1536]  (spike 0/1)
snn_head_golden_block1_out block1  float32[8][1536]  (spike 0/1)
snn_head_golden_fc2_out    li_out  float32[8][1536]  (mem_int32 * fc2.output_scale)
snn_head_golden_fc3_out    action  float32[8][7]     (acc_int32 * fc3.output_scale)
```

`snn_head_golden.c` 已随仓库提交（`snn_head_golden_ready=1`），常规使用无需重新生成；
当导出包或 PAIBox LIF lowering 变更时，必须在生成 artifact 的同一 PAIBox 环境中重跑
该脚本。生成环境需提供 PyTorch、snntorch、PAIBox 和 paicorelib；常规构建不运行此脚本。

## 构建与烧录

在 PAIRV 仓库根目录执行：

```bash
export NUCLEI_SDK_ROOT=$(pwd)
source setup.sh

# 构建指定层；每个子目录都是独立工程
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilmflashxip \
     PROGRAM=application/baremetal/snn_head/board_test/fc1_lif clean all

# 烧录并运行
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilmflashxip \
     PROGRAM=application/baremetal/snn_head/board_test/fc1_lif upload
```

切换测试项只需替换末级目录：`block0_lif` / `block1_lif` / `fc2` / `fc3` /
`chain`。每个 board test 均固定开启 timing；可用 `SNN_HEAD_DEBUG=1` 追加文本诊断。

`DOWNLOAD` 仅支持 `ilmflashxip` 或 `flashxip`：本工程将 SNN Head 的 artifact 与参数链入 flash，使用 `ilm` 将在构建期报错。

### 逐层中间结果输出

启用 `DUMP=1` 后，各层在 ②LayerNorm / ③量化 int8 / ④PAICore 原始输出 / ⑤本层输出 阶段全量输出；默认关闭，对部署路径无逐字节影响。

```bash
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilmflashxip \
     PROGRAM=application/baremetal/snn_head/board_test/fc1_lif DUMP=1 clean all
```

## 结果判定

下载和启动成功不代表测试通过。串口必须出现该 target 的 `GOLDEN PASS`；
`RUN FAILED`、`GOLDEN FAIL`、超时或仅有启动横幅都不计为通过。

## 约束

- 需真实 PAICore 硬件：涉及类脑核帧收发，QEMU 会超时。
- 单层 target 只生成并链接自己的 `assets/<layer>/compile_artifacts.bin`；
  `chain` 生成并链接全部五层 artifact。生成的 RISC-V 嵌入对象只位于当前
  target 的 `generated/<layer>/`，不是 asset，也不应提交。`make clean`
  会删除整个本地 `generated/`，包括旧构建配置遗留的其他层对象。
- 各层 artifact 使用 `timesteps=8` 的完整样本图。层内通过
  `rvrt_paicore_runner_run_sample()` 一次接收完整样本；runtime 对绝对 timestep `0..7`
  逐时间步执行“发送输入、累计 SYNC、IRQ 接收至 COMPLETE”，并把每步 RX work frame 散写到
  完整`[8][element]`输出。STREAM frame 去除`target_lcn`低位后的 timestamp 直接对应输出行，
  `pipeline_latency=1`不偏移输出行。reset 只发生在样本开始。
- 层间共享帧 buffer 固定为`256`帧，用于输入编码 chunk 和 reset completion；样本 completion
  的 DATA/VOLTAGE work frame 不保存在该数组中。
- 每个 SYNC 的 RX barrier 收到 COMPLETE 后立即结束，不读取该 COMPLETE 后的 uplink frame。
- 输入 work-frame 的 timestamp 以`(logical_timestep << target_lcn) + tick_relative`
编码。当前 LCN-5 artifact 的`0..7`完整输入窗口仍在 8-bit timestamp 范围内；completion 是
独立 control timeline target：当前`pipeline_latency=1`且`timesteps=8`，因此为`sync(8)`。
输出 mapping 的 tick-relative 只影响 work-frame 的 timestamp/address 和解码行号，不改变 sync target。
- 板级调试默认使用 `SoC/evalsoc/Board/nuclei_fpga_eval/openocd_evalsoc.cfg`，JTAG 配置不同的硬件需替换。

## 文件说明


| 文件                       | 用途                                                               |
| -------------------------- | ------------------------------------------------------------------ |
| `common.mk`                | 由子目录名推导 target/artifact，并提供共同模型与 timing 构建规则   |
| `<layer>/Makefile`         | 单行包含 `common.mk`                                                |
| `<layer>/main.c`           | 各层与 chain 的独立 `main`                                        |
| `<layer>/generated/`       | 当前 target 的临时 artifact 副本与 RISC-V 嵌入对象（忽略且可清理） |
| `golden/snn_head_golden.c` / `.h` | golden 输入与各层边界参考张量（已提交，`ready=1`）       |
| `gen_snn_head_golden.py`   | 从 INT8 QAT 导出包生成`snn_head_golden.c`                          |
| `snn_head_dump_sink.c`     | `SNN_HEAD_DUMP` 的串口输出实现（机器可解析格式）                   |
| `profile_report.h`         | 将共享 timing profile 输出为测试串口文本                           |
