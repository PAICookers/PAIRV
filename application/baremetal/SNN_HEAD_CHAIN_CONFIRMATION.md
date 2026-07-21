# SNN Head 手动编排流程与待确认清单

## 当前结论

本文件记录当前在不依赖完整 `ExecutionPlan` 的前提下，如何手动编排 SNN Head 的整体执行流程，以及后续必须确认的事项。

当前已合并 `origin/main` 的 runtime/schema 更新，4 份 PAIBox artifact 已经可以通过 `artifact_reader` 正常读取。读取过程只调用 `rvrt_artifact_read()`、`rvrt_artifact_get_info()`、`rvrt_artifact_thread_runtime()`、`rvrt_artifact_get_input_mapping_view()`、`rvrt_artifact_get_output_mapping_view()` 等公开接口，没有直接解析 `compile_artifacts.bin`。

## 当前 artifact 状态

已到位 artifact：

| 层 | 路径 | 读取状态 | 输入 mapping | 输出 mapping | 备注 |
|---|---|---|---|---|---|
| `fc1_lif` | `tests/runtime/fixtures/artifacts2/fc1_lif/runtime/compile_artifacts.bin` | OK | `768` entries, `INT8`, `bit_width=8`, `target_lcn=4` | `1536` entries, `DATA`, `UINT1`, `target_lcn=2` | 对应 `fc1 + lif_in` |
| `block0_lif` | `tests/runtime/fixtures/artifacts2/block0_lif/runtime/compile_artifacts.bin` | OK | `1536` entries, `INT8`, `bit_width=8`, `target_lcn=5` | `1536` entries, `DATA`, `UINT1`, `target_lcn=2` | 对应 `block0.fc + LIF` |
| `block1_lif` | `tests/runtime/fixtures/artifacts2/block1_lif/runtime/compile_artifacts.bin` | OK | `1536` entries, `INT8`, `bit_width=8`, `target_lcn=5` | `1536` entries, `DATA`, `UINT1`, `target_lcn=2` | 对应 `block1.fc + LIF` |
| `fc2` | `tests/runtime/fixtures/artifacts2/fc2/runtime/compile_artifacts.bin` | OK | `1536` entries, `INT8`, `bit_width=8`, `target_lcn=5` | `1536` entries, `VOLTAGE`, `INT32`, `target_lcn=4` | 对应 `fc2 + li_out` 膜电位输出 |

仍缺 artifact：

- `fc3`：最终 `1536 -> 7` 输出层，**已确认后续会补 artifact**。

所有 4 份 artifact 的 runtime 参数一致：

```text
timesteps=8
tick_depth=1
sync_steps=8
decode_mode=0
thread_count=1
schema_version=1
```

## ExecutionPlan 当前状态

当前 4 份 artifact 都能通过 `rvrt_artifact_read()`，但 `ExecutionPlan` 为空：

```text
buffers: count=0
plan: stages=0
cpu_tasks=0
paicore_phases=0
capacity: missing field
```

因此当前不能直接使用 `artifact_executor` 自动执行完整 stage。现阶段应采用手动编排方式：

1. 每层 artifact 仍通过 `artifact_reader` 读取。
2. 每层 config frames 仍通过 `rvrt_artifact_config_frame_words()` 获取并下发。
3. 每层输入编码通过 `rvrt_artifact_get_input_mapping_view()` + `rvrt_encode_input_chunk()` 完成。
4. 每层输出解码通过 `rvrt_artifact_get_output_mapping_view()` 后按 kind 分流：
   - `DATA + UINT1`：走 `rvrt_decode_output_frame()`。
   - `VOLTAGE + INT32`：走 `rvrt_decode_voltage_frame()`。
5. 层间 CPU 算子和 buffer 管理由手动流程显式组织，不依赖 `ExecutionPlan.stages`。

## 手动编排版整体流程

### 输入约定

SNN Head 的上游输入为：

```text
x: float32[8, 768]
```

其中 `8` 是 action chunk 的 timestep 数，也是 SNN 在一个 chunk 内展开的时间步。跨 chunk 时 LIF 状态应清零；一个 chunk 内 8 个 timestep 的 LIF 状态连续演化。

### 总体执行顺序

手动编排的整体链路如下：

```text
输入 x[8,768]
  -> CPU: LN1(768)
  -> CPU: quantize to int8, scale=fc1 activation_scale
  -> PAICore: fc1_lif artifact, 输入 int8[8,768], 输出 spike/DATA[8,1536]
  -> CPU: block0 LN over [8,1536]
  -> CPU: quantize to int8, scale=block0 activation_scale
  -> PAICore: block0_lif artifact, 输入 int8[8,1536], 输出 spike/DATA[8,1536]
  -> CPU: block1 LN over [8,1536]
  -> CPU: quantize to int8, scale=block1 activation_scale
  -> PAICore: block1_lif artifact, 输入 int8[8,1536], 输出 spike/DATA[8,1536]
  -> CPU: LN2 over [8,1536]
  -> CPU: quantize to int8, scale=fc2 activation_scale
  -> PAICore: fc2 artifact, 输入 int8[8,1536], 输出 li_out membrane/VOLTAGE int32[8,1536]
  -> CPU: dequantize fc2 membrane int32 -> float32[8,1536]
  -> CPU: quantize li_out float32 -> fc3 int8 input[8,1536]
  -> PAICore: fc3 artifact, 输入 int8[8,1536], 输出 acc_int32[8,7]
  -> CPU: dequantize fc3 acc_int32 -> fp32 action[8,7]
```

当前只有前 4 个 PAICore artifact 到位，因此可推进到：

```text
fc2 输出 int32 membrane[8,1536]
```

最终 `fc3` 已确认后续补 artifact，并按 `SNN_Head_PAICore_Deployment_Guide.md` §2.1 路线执行：`li_out` 膜电位先在 CPU 侧反量化为 fp32，再量化为 fc3 的 int8 输入；`fc3` 在 PAICore 上执行 INT8 Linear，输出 `acc_int32[8,7]` 后由 CPU 反量化为最终 `fp32 action[8,7]`。

## 每层 PAICore 手动执行模板

对每一份按层 artifact，手动流程应一致：

1. **读取 artifact**
   - 用 `rvrt_artifact_read()` 读取 `compile_artifacts.bin`。
   - 用 `rvrt_artifact_get_info()` 确认 `schema_version`、`thread_count`、`config_word_count`。
   - 用 `rvrt_artifact_thread_runtime()` 确认 `timesteps=8`、`sync_steps=8`。

2. **下发 config frames**
   - 用 `rvrt_artifact_config_word_count()` / `rvrt_artifact_config_frame_words()` 遍历 config frames。
   - 通过 NoC/FIFO 下发到 PAICore。
   - 每一层单独配置，不能假设不同 artifact 共享配置。

3. **准备 8 个 timestep 的输入 buffer**
   - 每层输入必须先在 CPU 侧准备完整 8 个 timestep。
   - `fc1_lif` 输入是 `int8[8,768]`。
   - `block0_lif`、`block1_lif`、`fc2` 输入是 `int8[8,1536]`。

4. **编码输入 work frames**
   - 用 `rvrt_artifact_get_input_mapping_view()` 获取 input mapping。
   - 对 `t=0..7` 调用 `rvrt_input_cursor_init(cursor, t)` 后编码该 timestep 输入。
   - 使用 `rvrt_encode_input_chunk()` 生成 work frames。
   - 当前 mapping 的 `entry_count` 是单个 timestep 的特征维度：`768` 或 `1536`，不是 `8 * dim`。

5. **触发 / 同步 PAICore 执行**
   - 依据 `runtime.sync_steps=8` 构造同步控制帧。
   - 当前没有 `paicore_phase.latency_ticks` 字段可用，因为 `ExecutionPlan.paicore_phases` 为空；因此同步/等待策略需要单独确认。

6. **接收并解码输出 frames**
   - 用 `rvrt_artifact_get_output_mapping_view()` 获取 output mapping。
   - 如果 `kind=DATA`，使用 `rvrt_decode_output_frame()` 解码 spike 输出。
   - 如果 `kind=VOLTAGE`，使用 `rvrt_decode_voltage_frame()` 解码膜电位输出。
   - `fc2` 的输出已确认为 `VOLTAGE + INT32`，必须走 `rvrt_decode_voltage_frame()`。

7. **保存完整 8 timestep 中间结果**
   - 每层输出都要保存完整 `[8, dim]`。
   - 不能只保存最后一个 timestep。
   - 后续 LayerNorm/量化都按完整 `[8, dim]` 的每个 timestep 继续处理。

## CPU 侧算子职责

手动编排中，以下逻辑不在 artifact 内，需要 CPU 侧显式执行：

- `LN1(768)`
- `block0.LN(1536)`
- `block1.LN(1536)`
- `LN2(1536)`
- 每层输入量化：`float32 -> int8`
- `fc2` 膜电位输出反量化：`int32 -> float32`
- `li_out -> fc3` 输入转换：`float32 -> int8`
- `fc3` 输出反量化：`acc_int32[8,7] -> fp32 action[8,7]`
- 层间 buffer 管理：采用单最大 workspace 覆盖式复用，不同时常驻保存所有 dtype 的中间结果
- 每个 chunk 开始前，PAICore/LIF 状态应清零；一个 chunk 内 8 个 timestep 状态连续演化

## 当前最重要的待确认项

### A. 已确认决策

- [x] `fc3` artifact 后续会补齐，并按 §2.1 作为 PAICore INT8 Linear 执行。
- [x] `fc3` artifact 的内容边界与现有 FC 层 artifact 一致：提供 PAICore config frames、input mapping、output mapping 等运行时结构信息。
- [x] `fc3.acc_int32[8,7]` 最终由 CPU 反量化为 `fp32 action[8,7]`，反量化 scale 从 INT8 QAT 导出包/预置参数读取。
- [x] 手动编排是正式路线，不依赖完整 `ExecutionPlan`。
- [x] 当前 4 份 artifact 的 `ExecutionPlan` 为空不阻塞手动编排路线。
- [x] 跨 action chunk（即跑完一个完整 8 timestep 后进入下一个 8 timestep）时，所有 LIF/膜电位状态清零；一个 chunk 内 8 个 timestep 状态连续保留。
- [x] 向芯片下发 config 后，芯片默认自动 init；显式 `init frame` 用于在已完成 config 且已经跑过一次任务后，不重新下发 config、直接跑第二次任务前重置状态。
- [x] artifact 读取必须继续通过 `artifact_reader`，不能直接解析 `compile_artifacts.bin`。
- [x] 当前手动编排每层发送 1 个 sync frame，payload 使用该层 artifact 的 `runtime.sync_steps=8`；若未来接入完整 `ExecutionPlan`，自动执行器路线再使用 PAIBox 提供的 `phase.latency_ticks`。
- [x] `target_lcn=4/5` 在当前硬件配置和固定 8 timestep 流程下不会影响最大 timestep 容量或输出接收估算。
- [x] 上板中间 buffer 采用单最大 workspace 覆盖式复用：第一层输入写入 workspace，本层输出覆盖输入，后续每层重复覆盖；不同时常驻保留 `[8,1536]` 的 spike/int8/int32/float32 全部调试副本。

### B. 可由现有代码确认的事项

- [x] `rvrt_input_cursor_init(cursor, t)` 中的 `t=0..7` 是 runtime timestep，会被编码进 work frame。
- [x] input mapping 的 `entry_count=dim` 表示每次编码一个 timestep 的 `dim` 个元素，不是 `8 * dim`。
- [x] `target_lcn` 和 `tick_relative` 已由 artifact mapping 提供，CPU 侧通过 `rvrt_encode_input_chunk()` 自动组合成 frame timestep，不需要手动展开。
- [x] 输入 payload 为 0 时，`rvrt_encode_input_chunk()` 会跳过该 entry，不发送零 payload frame。
- [x] `DATA + UINT1` 输出应通过 `rvrt_decode_output_frame()` 解码为 `uint8` 形式的 spike 值。
- [x] `fc2` 的 `VOLTAGE + INT32` 输出读出的是膜电平帧；通过 `rvrt_decode_voltage_frame()` 解码后就是 `li_out` 进入 `fc3` 前使用的 signed int32 膜电位。
- [x] `rvrt_decode_voltage_frame()` 通过地址识别 4 个 8-bit lane，收齐后按位拼成 `int32_t`，不需要额外符号扩展。
- [x] `rvrt_session_sync_wait()` 的完成条件是收到 complete frame；`timeout_ms` 只用于防止死等。
- [x] 当前 session/runtime 设计假设每次 sync phase 的输出序列为：该层所有 output work frames 在前，最后 1 个 complete frame 收尾；收到第一个 complete frame 后本 phase 结束。
- [x] `rx_capacity` 至少需要覆盖所有输出 work frames 加 complete frame；`VOLTAGE` 每个 int32 元素需要 4 个 lane frame。
- [x] CPU 侧已有可复用算子：`rv_layernorm_f32()`、量化/反量化接口、`rv_fc_s8_s32()`。
- [x] `rvrt_session_load_config()` 只遍历并下发 artifact config frames，不会自动发送 init/reset 控制帧。
- [x] 现有手动 session 示例采用 `config -> input frames -> sync_wait` 流程；该流程依赖芯片收到 config 后默认完成 init。`rvrt_build_init_frame()` 目前只在 codec 单测中验证控制帧构造能力。

### C. 仍需结合芯片/PAIBox侧确认的事项

- [ ] `fc3` artifact 到位后，用 `artifact_reader` 验收 input/output mapping、输出 kind/dtype、config word 数量和 runtime 参数。

## 可先推进的最小验证任务

在不等待 `fc3`、不等待完整 `ExecutionPlan` 的情况下，可以先推进：

1. **reader 验收固定化**
   - 固定记录 4 份 artifact 的 `info/runtime/input_mapping/output_mapping`。
   - 确认 `fc2` 输出为 `VOLTAGE + INT32`。

2. **单层输入编码验证**
   - 对 `fc1_lif` 构造 `int8[8,768]` 假输入。
   - 验证 `rvrt_encode_input_chunk()` 对 8 个 timestep 生成 frame 数和地址符合预期。

3. **单层输出解码验证**
   - 对 `fc1/block0/block1` 验证 DATA 解码路径。
   - 对 `fc2` 验证 VOLTAGE 解码路径。

4. **CPU 算子串接 dry-run**
   - 不上板，只用假 PAICore 输出，先串通 LN/quant/dequant 的 buffer 形状。

5. **等待 `fc3` artifact 并接入最终输出**
   - `fc3` artifact 到位后，读取 input/output mapping 和 dtype。
   - 按 §2.1 执行 `li_out int32 -> fp32 -> fc3 int8` 输入转换。
   - PAICore 执行 `fc3(1536->7)` 后，CPU 将 `acc_int32[8,7]` 反量化为 `fp32 action[8,7]`。

## 当前决策

- 不依赖完整 `ExecutionPlan`。
- 不使用 `artifact_executor` 自动编排。
- artifact 读取必须继续通过 `artifact_reader`。
- 按层 artifact 手动下发 config、手动编码输入、手动同步、手动解码输出。
- `fc2` 输出必须按 `VOLTAGE + INT32` 处理。
- `fc3` 后续补 artifact，并按 §2.1 在 PAICore 执行 INT8 Linear。
- `fc3.acc_int32[8,7]` 最终由 CPU 反量化为 `fp32 action[8,7]`。
