# Generated FlatBuffers Runtime Binding

This directory contains generated code consumed by the PAIRV runtime.
`compile_artifacts_generated.h` is generated from the PAIBox
`backendv2/schemas/compile_artifacts.fbs` schema and must not be edited by
hand. Regenerate and synchronize the schema, binding, fixtures, and runtime
reader together.

## FlatBuffers Runtime 字段参考

本节在 PAIBox `backendv2/schemas/README.md` 与 PAIRV
`Lib/runtime/generated/README.md` 中保持一致。PAIBox 负责定义 schema，
PAIRV 使用生成的 C++ binding 读取二进制。修改字段契约时必须同步更新
schema、generated binding、fixture、reader 和本节文档。

### 总体结构

```text
CompileArtifacts
├── schema_version
├── io_mapping
│   └── threads[]
│       ├── thread_id
│       ├── root_core_offset {xy, x, y}
│       ├── runtime {timesteps, tick_depth, sync_steps, decode_mode}
│       ├── input_mappings
│       │   └── items[]: InputTensorMapping
│       │       ├── name, shape, bit_width
│       │       └── entries[]: InputEntry
│       ├── output_mappings
│       │   ├── target_lcn
│       │   └── items[]: OutputTensorMapping
│       │       ├── name, shape, kind, dtype
│       │       └── entries[]: OutputEntry
│       └── core_ticks[]: CoreTick
├── config_frames
│   ├── words[]
│   └── word_order
└── execution_plan (optional for manual PAICORE control)
    ├── runtime_target
    ├── buffers[]
    ├── cpu_tasks[]
    ├── paicore_phases[]
    └── stages[]
```

FlatBuffers root table 为 `CompileArtifacts`，file identifier 为 `PBCA`。
`schema_version` 表示字段契约版本，不标识具体模型。

### 通用数据结构

| 结构 | 字段 | 含义 |
| --- | --- | --- |
| `CoreOffset` | `xy`, `x`, `y` | 相对 root core 的路由坐标偏移。 |
| `CopyCount` | `xy`, `x`, `y` | 对应路由维度上的复制数量。 |
| `Shape` | `size[]` | tensor 完整逻辑 shape，采用连续 C-order。 |
| `TickParams` | `tick_start` | PAICORE 节点开始工作的硬件 tick；仅由 `CoreTick.tick` 使用。 |
| `TickParams` | `tick_duration` | 节点持续工作的 tick 数。 |
| `TickParams` | `tick_initial` | 节点初始 tick 参数。 |

### I/O Mapping

`IOMapping.threads[]` 保存各 runtime thread 的静态 PAICORE 输入、输出映射。

| 结构 | 字段 | 含义 |
| --- | --- | --- |
| `ThreadIOMapping` | `thread_id` | thread 的逻辑编号。 |
|  | `root_core_offset` | 该 thread 的路由坐标基准。 |
|  | `runtime` | timestep、tick depth、sync 和 decode 参数。 |
|  | `input_mappings` | 输入 tensor 到 PAICORE work frame 的映射集合。 |
|  | `output_mappings` | PAICORE DATA/VOLTAGE frame 到输出 tensor 的映射集合。 |
|  | `core_ticks[]` | 已部署 core 的 tick 配置和关联节点名称。 |
| `RuntimeParams` | `timesteps` | 一个 sample 的逻辑 timestep 数。 |
|  | `tick_depth` | PAICORE 计算图的硬件 tick 深度。 |
|  | `sync_steps` | sample 执行时发送的 sync phase 数。 |
|  | `decode_mode` | 输出按连续流或按 step 解码。 |
| `CoreTick` | `core_offset` | core 相对 root 的坐标。 |
|  | `tick` | 该 core 的硬件 tick 参数。 |
|  | `nodes[]` | 映射到该 core 的 PAIIR 节点名称，仅用于描述和调试。 |

#### 输入映射

| 结构 | 字段 | 含义 |
| --- | --- | --- |
| `InputTensorMappings` | `items[]` | 一个 thread 的输入 tensor mapping 列表。 |
| `InputTensorMapping` | `name` | 输入 tensor 的逻辑名称。 |
|  | `shape` | 输入 tensor 的完整 shape。 |
|  | `bit_width` | PAICORE DATA payload 位宽。 |
|  | `entries[]` | 每个逻辑元素的 work frame 编码信息。 |
| `InputEntry` | `elem_idx` | 连续输入 buffer 中的扁平元素下标。当前低位宽元素也各占 1 byte。 |
|  | `core_offset` | work frame 的目标 core 路由偏移。 |
|  | `copy_count` | frame 路由复制数量。 |
|  | `tick_relative` | 当前 LCN 分组内的相对 tick。 |
|  | `addr_axon` | 目标 axon 地址。 |
|  | `target_lcn` | LCN 的指数编码，见“LCN 编码”。 |
|  | `copy_id` | 写入 work frame 的 copy selector。 |
|  | `dtype` | 输入 payload 的符号和位宽。 |

输入编码顺序遍历全部 `entries[]`，并直接读取 `input[elem_idx]`，复杂度为
O(N)。输入 entry 不需要排序，也不需要二分查找。

#### 输出映射

| 结构 | 字段 | 含义 |
| --- | --- | --- |
| `OutputTensorMappings` | `target_lcn` | 当前 thread 输出映射共享的目标 LCN。 |
|  | `items[]` | 一个 thread 的输出 tensor mapping 列表。 |
| `OutputTensorMapping` | `name` | 输出 tensor 的逻辑名称。 |
|  | `shape` | 输出 tensor 的完整 shape。 |
|  | `kind` | 帧协议和解码路径：DATA 激活值或 VOLTAGE 膜电位。 |
|  | `dtype` | 解码后 tensor 的元素类型，也是输出位宽的唯一来源。 |
|  | `entries[]` | frame 侧位置到逻辑 tensor 元素的映射。 |
| `OutputEntry` | `elem_idx` | 连续输出 buffer 中的扁平元素下标。 |
|  | `copy_id` | 期望的 frame copy selector。 |
|  | `axon_bit_idx` | 将 frame axon 地址与 bit 位置展开后的查找键。 |

`kind` 与 `dtype` 的职责不同：`kind` 决定接收哪类 PAICORE 帧以及采用哪条
解码路径；`dtype` 决定解码后 tensor 的元素类型、位宽和连续 buffer 中的单元素
存储字节数。输出 mapping 不重复保存 `bit_width`，需要显示或校验位宽时由
`dtype` 推导。

同一 output mapping 只能有一种 `kind/dtype` 组合。DATA 只允许有符号或无符号
1/2/4/8-bit dtype；VOLTAGE 只允许 `INT32`；`INT64` 只用于 CPU runtime tensor。

同一 output mapping 的 entry 必须按 `axon_bit_idx` 严格升序且 key 唯一。
PAIBox 在导出前排序并拒绝重复 key；PAIRV 在读取 artifact 时再次验证，运行时
只使用二分查找，不接受无序回退。

### LCN 编码

`target_lcn` 保存 PAICORE frame 编码使用的二进制扩展指数：

| 编码值 | LCN |
| ---: | ---: |
| 0 | 1X |
| 1 | 2X |
| 2 | 4X |
| 3 | 8X |
| 4 | 16X |
| 5 | 32X |
| 6 | 64X |
| 7 | 128X |

当前 offline-core runtime 只接受 `0..7`。字段在 FBS/proto 和 PAIRV C ABI 中
分别保持 `uint32`/`uint32_t`；artifact reader 在边界校验范围，不额外定义 LCN enum。

### DataType 与存储

| `DataType` | 单元素存储 | 使用范围 |
| --- | ---: | --- |
| `NOT_SET` | 无 | 缺失值；可执行 mapping 或 runtime buffer 中视为非法。 |
| `UINT1`, `INT1` | 1 byte | PAICORE DATA 与 runtime tensor。 |
| `UINT2`, `INT2` | 1 byte | PAICORE DATA 与 runtime tensor。 |
| `UINT4`, `INT4` | 1 byte | PAICORE DATA 与 runtime tensor。 |
| `UINT8`, `INT8` | 1 byte | PAICORE DATA 与 runtime tensor。 |
| `INT32` | 4 bytes | PAICORE VOLTAGE 膜电位输出与对应 runtime tensor。不可作为 PAICORE DATA payload。 |
| `INT64` | 8 bytes | CPU task 的索引输出，例如 `torch.argmax`。不可作为 PAICORE DATA payload。 |

低位宽 runtime buffer 当前不是 bit-packed。逻辑字节数按
`dtype 存储字节数 * product(shape)` 推导，不在 schema 中重复保存。
signed 1/2/4/8-bit DATA 使用二进制补码，PAIRV 解码后进行符号扩展。

### 其他枚举

| 枚举 | 值 | 含义 |
| --- | --- | --- |
| `OutputKind` | `DATA` | 激活值输出，位宽不超过 8 bit。 |
|  | `VOLTAGE` | 膜电位输出，解码为 `INT32` tensor。 |
| `DecodeMode` | `STREAM` | 按连续输出流解释帧。 |
|  | `STEP` | 按逻辑 step 解释帧。 |
| `WordOrder` | `HIGH_FIRST` | 每对 config word 的 high word 在前。 |
|  | `LOW_FIRST` | 每对 config word 的 low word 在前。 |
| `RuntimeBufferKind` | `TENSOR` | 连续 C-order tensor buffer；当前唯一支持类型。 |
| `ExecutionStageKind` | `PAICORE` | 执行一个 PAICORE phase。 |
|  | `CPU_TASK` | 执行一个 generated CPU task。 |

### ExecutionPlan

`ExecutionPlan` 是 target-local 的有序 runtime 调度计划。基础 artifact reader、
I/O mapping codec 和 session 不要求它存在；只有 `artifact_executor` 解释自动调度
时要求完整 plan。

| 结构 | 字段 | 含义 |
| --- | --- | --- |
| `ExecutionPlan` | `runtime_target` | 当前 target 的 id、profile 和 task ABI 要求。 |
|  | `buffers[]` | 所有 stage 共享的逻辑 tensor buffer 表。 |
|  | `cpu_tasks[]` | generated CPU task 调用表。 |
|  | `paicore_phases[]` | PAICORE runtime phase 表。 |
|  | `stages[]` | sample 的有序执行序列。 |
| `RuntimeTarget` | `target_id` | package 内的逻辑 target id，例如 `rv_cpu0`。 |
|  | `profile_id` | PAIBox codegen 选用的 target capability profile。 |
|  | `required_task_abi_version` | artifact 要求的最低 `rvrt_tasks` ABI 版本。 |
| `RuntimeBuffer` | `kind` | buffer 类型，当前仅 `TENSOR`。 |
|  | `dtype` | tensor 元素类型。 |
|  | `shape` | tensor 完整逻辑 shape。 |
| `CpuTask` | `input_ref` | 输入 tensor 在 `ExecutionPlan.buffers[]` 中的下标。 |
|  | `output_ref` | 输出 tensor 在 `ExecutionPlan.buffers[]` 中的下标。 |
| `PaicorePhase` | `input_ref` | 编码并发送到 PAICORE 的 runtime buffer 下标。 |
|  | `output_ref` | 接收并解码 PAICORE DATA 的 runtime buffer 下标。 |
|  | `input_mapping_ref` | `ThreadIOMapping.input_mappings.items[]` 下标。 |
|  | `output_mapping_ref` | `ThreadIOMapping.output_mappings.items[]` 下标。 |
|  | `latency_ticks` | PAIBox 计算的 phase 输入到输出延迟；CPU task 不贡献 PAICORE tick。 |
| `ExecutionStage` | `stage_index` | 从 0 开始且单调递增的执行序号。 |
|  | `kind` | 选择 PAICORE 或 CPU task executor。 |
|  | `ref_index` | 按 `kind` 索引 `paicore_phases[]` 或 `cpu_tasks[]`。 |

所有 `*_ref` 和 `ref_index` 都是从 0 开始的数组下标，不是指针，也不是
二进制 byte offset。相邻 stage 必须满足：

```text
previous.output_ref == next.input_ref
```

当前 PAIRV 支持单 active session 和 ordered stage sequence，不是通用 DAG
scheduler。手动 app 可只依赖 config frames、thread runtime 和 I/O mappings：

```text
artifact_read -> session_init -> session_load_config
-> input_cursor_init -> encode_input_chunk -> session_send_frames
-> session_sync_wait -> DATA/VOLTAGE decoder
```

app 自行选择一次屏障前编码一个或多个 timestep；session 不提供 window、batch、
callback 或 input-provider 抽象。

`CpuTask` 只描述输入、输出 buffer，不描述算子实现。算子函数体位于 PAIBox
生成的 `rvrt_tasks.c/.h`，PAIRV 检查 task ABI 后按 local task index 调用。

`PaicorePhase` 不携带配置帧。PAICORE 配置属于整个 package，在 sample 执行前
全局加载一次；phase 只选择 runtime buffer、静态 I/O mapping 和输入到输出延迟。
当前 PAIRV 对含 CPU task 的 execution plan 只支持 `timesteps=1`，并直接以
`latency_ticks` 作为该 phase 控制帧的 sync delta。多时间步流水需要后续的全局
tick scheduler，不在当前契约内。

### ConfigFrames

| 字段 | 含义 |
| --- | --- |
| `words[]` | 32-bit word 数组；相邻两个 word 组成一个 64-bit NoC 配置帧。 |
| `word_order` | 决定每对 word 中 high/low 的排列方式。 |

```text
HIGH_FIRST: high = words[2*i],     low = words[2*i + 1]
LOW_FIRST:  low  = words[2*i],     high = words[2*i + 1]
```

`words[]` 长度必须为偶数。PAIRV 每次读取完整 high/low pair，并在首次推理前由
app 或 executor 调用基础 session API 一次性加载全局配置。

### 文件所有权

- PAIBox `compile_artifacts.fbs` 是字段定义的唯一来源。
- PAIRV `compile_artifacts_generated.h` 是生成文件，不可手工修改。
- `compile_artifacts.bin`、对应 schema 与 generated `rvrt_tasks.c/.h` 必须来自
  同一次 target package 导出，不得混用。
