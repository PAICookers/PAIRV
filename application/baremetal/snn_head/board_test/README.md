# SNN Head Board Test (board_test)

在真实 **PAICore（N307FD RISC-V CPU + 类脑核）** 硬件上，对完整 SNN Head 进行分层与端到端验证的裸机测试工程。

每个测试为独立可执行 ELF，经完整 PAICore runtime（session / frame_codec / artifact 编解码）向类脑核发送并等待帧，因此仅可在真实硬件上运行。QEMU 仅仿真 N307FD 的 RISC-V CPU，不含类脑核，测试将在 `sync_wait` 处超时（2000 ms）。若仅需在 QEMU 上验证 CPU 侧算子（LayerNorm / 量化 / FC / 反量化），请使用 `application/baremetal/snnhead_real_ops_selftest`。

## 测试项

通过 `LAYER` 变量选择，每项对应 `layers/test_$(LAYER).c` 中的独立 `main`：


| LAYER        | 覆盖范围                                     | 备注                                          |
| ------------ | -------------------------------------------- | --------------------------------------------- |
| `fc1_lif`    | LN1 → fc1 → lif_in                         | 默认值                                        |
| `block0_lif` | block0：LN → fc → LIF                      |                                               |
| `block1_lif` | block1：LN → fc → LIF                      |                                               |
| `fc2`        | LN2 → fc2 → li_out（膜电位）               |                                               |
| `fc3`        | fc3 → action                                | 需`fixtures/fc3/compile_artifacts.bin`        |
| `chain`      | fc1 → block0 → block1 → fc2 → fc3 端到端 | 五层共享同一 tensor_workspace，需 fc3 fixture |

## 验证模型

测试不在板上进行数值判定，仅执行真实层函数并经串口输出结果，由 PC 侧读取后与 golden 逐元素比对。

- **单层测试**：向目标层输入 `golden_input`，执行该层函数。中间与最终张量由层内 `SNN_HEAD_DUMP` 经串口输出（需 `DUMP=1`）。单层测试隔离供给 golden 输入，用于定位单层正确性，不反映层间真实数据通路。
- **端到端测试（`chain`）**：执行 `snn_head_run_chunk()`，五层经同一 tensor_workspace 接力，完整走过 LayerNorm / 量化 / stride 扩排 / spike 拓宽 / 帧编解码的真实数据搬运路径。无论是否启用 `DUMP`，均以机器可解析格式输出最终 `action[8][7]`。

浮点输出格式为 `<idx> 0x<IEEE754 位> <round(v*1e6)>`，可在 PC 侧精确还原浮点位。

## Golden 数据

`snn_head_golden.c` 及各层边界参考张量由 `gen_snn_head_golden.py` 生成。该脚本从 INT8 QAT 导出包重建 INT8 runtime（无需浮点权重），对固定确定性输入前向，抓取各层边界张量，其表示与 C 侧 `tensor_workspace` 一致：

```text
snn_head_golden_input      x       float32[8][768]
snn_head_golden_fc1_out    lif_in  float32[8][1536]  (spike 0/1)
snn_head_golden_block0_out block0  float32[8][1536]  (spike 0/1)
snn_head_golden_block1_out block1  float32[8][1536]  (spike 0/1)
snn_head_golden_fc2_out    li_out  float32[8][1536]  (mem_int32 * fc2.output_scale)
snn_head_golden_fc3_out    action  float32[8][7]     (acc_int32 * fc3.output_scale)
```

`snn_head_golden.c` 已随仓库提交（`snn_head_golden_ready=1`），常规使用无需重新生成；仅在导出包更新时重跑该脚本。

## 构建与烧录

在 PAIRV 仓库根目录执行：

```bash
export NUCLEI_SDK_ROOT=$(pwd)
source setup.sh
export PATH=$NUCLEI_TOOL_ROOT/gcc/riscv64-unknown-elf/bin:$PATH

# 构建指定层
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilmflashxip \
     PROGRAM=application/baremetal/snn_head/board_test LAYER=fc1_lif clean all

# 烧录并运行
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilmflashxip \
     PROGRAM=application/baremetal/snn_head/board_test LAYER=fc1_lif upload
```

切换测试项修改 `LAYER` 即可：`block0_lif` / `block1_lif` / `fc2` / `fc3` / `chain`。

`DOWNLOAD` 仅支持 `ilmflashxip` 或 `flashxip`：本工程将 SNN Head 的 artifact 与参数链入 flash，使用 `ilm` 将在构建期报错。

### 逐层中间结果输出

启用 `DUMP=1` 后，各层在 ②LayerNorm / ③量化 int8 / ④PAICore 原始输出 / ⑤本层输出 阶段全量输出；默认关闭，对部署路径无逐字节影响。

```bash
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilmflashxip \
     PROGRAM=application/baremetal/snn_head/board_test LAYER=fc1_lif DUMP=1 clean all
```

## 结果判定（PC 侧）

从串口捕获输出后逐元素比对：

- 单层：对照 `snn_head_golden_<层>_out`（spike 层比对 0/1，fc2/fc3 比对浮点值）。
- `chain`：提取 `>>> DUMP chain.action ... <<< END chain.action` 段的 56 个浮点值，与 `golden_fc3_out` 比对。

## 约束

- 需真实 PAICore 硬件：涉及类脑核帧收发，QEMU 会超时。
- `fc3` 与 `chain` 需 `../fixtures/fc3/compile_artifacts.bin`；其余层不受影响（`--gc-sections` 回收未引用的 fc3 符号）。
- 各层 artifact 须为 `timesteps=1` 单步图，否则 `snn_head_run_chunk` 在对应层校验失败并输出 `RUN FAILED`。
- 板级调试默认使用 `SoC/evalsoc/Board/nuclei_fpga_eval/openocd_evalsoc.cfg`，JTAG 配置不同的硬件需替换。

## 文件说明


| 文件                       | 用途                                                               |
| -------------------------- | ------------------------------------------------------------------ |
| `Makefile`                 | 按`LAYER` 构建单层/端到端 ELF，链接层实现、CPU 参数与按层 artifact |
| `layers/test_*.c`          | 各层与 chain 的独立`main`                                          |
| `snn_head_golden.c` / `.h` | golden 输入与各层边界参考张量（已提交，`ready=1`）                 |
| `gen_snn_head_golden.py`   | 从 INT8 QAT 导出包生成`snn_head_golden.c`                          |
| `snn_head_dump_sink.c`     | `SNN_HEAD_DUMP` 的串口输出实现（机器可解析格式）                   |
