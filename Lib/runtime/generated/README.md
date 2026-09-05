# `compile_artifacts` 产物与读取指南

`compile_artifacts` 是 PAIBox 工具链导出的 FlatBuffers 模型产物。三种文件
各有不同职责：

| 文件                              | 内容                                | 使用者                    |
| --------------------------------- | ----------------------------------- | ------------------------- |
| `compile_artifacts.fbs`         | schema 定义                         | 编译器、`flatc`、审阅者 |
| `compile_artifacts.bin`         | 按 schema 编码的二进制 artifact     | PAIRV runtime             |
| `compile_artifacts_generated.h` | `flatc --cpp` 生成的 C++ accessor | C++ 工具、调试代码        |

运行时加载的是 `.bin`，不是 `.fbs` 或生成头文件。`.bin` 是普通
FlatBuffers buffer：不带 size prefix，根表为 `CompileArtifacts`，首地址需满足
FlatBuffers 对齐要求。当前 runtime 支持的 schema version 是 `1`。

## Schema 来源与再生成

权威 schema：

```text
PAIBox/paibox/backendv2/schemas/compile_artifacts.fbs
```

PAIRV 中用于生成绑定的镜像：

```text
application/baremetal/flatbuffers/assets/compile_artifacts.fbs
```

再生成命令：

```sh
flatc --cpp --gen-mutable --reflect-names \
  -o Lib/runtime/generated \
  application/baremetal/flatbuffers/assets/compile_artifacts.fbs
```

最近同步检查结果：PAIRV 镜像与 PAIBox `dev` 分支的 schema
仅在注释上有差异，按当前生成命令得到的输出一致。
`compile_artifacts_generated.h` 及对应再生成结果的 SHA-256 为
`9d76750b95820402cebfe16c4f0c37dccb4ad6ac0e3552c510afec10eada104b`。
更新 schema 后应同时刷新镜像、生成头文件和依赖 artifact，并重新运行 host tests。

## 当前数据树

```text
CompileArtifacts
├── schema_version
├── io_mapping
│   └── threads[]
│       ├── thread_id
│       ├── root_core_offset { xy, x, y }
│       ├── runtime { timesteps, tick_depth, sync_steps, decode_mode }
│       ├── input_mappings.items[]
│       │   └── InputTensorMapping { name, shape, bit_width, tick, entries[] }
│       ├── output_mappings { target_lcn, items[] }
│       │   └── OutputTensorMapping { name, shape, kind, bit_width, tick, entries[] }
│       └── core_ticks[]
└── config_frames { words, word_order }
```

每个 input/output mapping 的 `entries[]` 以 `axon_bit_idx` 作为查找键；它是
原始 9-bit work-frame 地址，不是 `target_lcn`。`target_lcn` 是输出通道的
3-bit 逻辑地址（`0..7`）。

`tick` 的类型是 `TickParams { tick_start, tick_duration, tick_initial }`；
输入 entry 还包含 `elem_idx`、core/copy 路由、`tick_relative`、`addr_axon`、
`target_lcn`、`copy_id` 和 `dtype`。输出 entry 包含 `elem_idx`、`copy_id`、
`axon_bit_idx` 和 `dtype`。这些 entry 是 frame 地址映射，不是连续 tensor
存储的描述；实际输出 stride 由 runtime 根据 mapping 计算。

### 枚举

| 枚举           | 当前值                                                                                |
| -------------- | ------------------------------------------------------------------------------------- |
| `DataType`   | `NOT_SET`, `UINT1`, `INT1`, `UINT2`, `INT2`, `UINT4`, `INT4`, `UINT8`, `INT8` |
| `OutputKind` | `DATA`, `VOLTAGE`                                                                 |
| `DecodeMode` | `STREAM`, `STEP`                                                                  |
| `WordOrder`  | `HIGH_FIRST`, `LOW_FIRST`                                                         |

当前生产 runtime 不从 schema 读取 `ExecutionPlan`、`RuntimeTarget`、
`RuntimeBuffer`、`CpuTask`、`PaicorePhase` 或 `ExecutionStage`；旧的
ExecutionPlan 示例仅保留在 `Lib/runtime/experimental/legacy/`。

`RuntimeParams.tick_depth`、`sync_steps` 和 `decode_mode` 在稳定 C view 中分别
规范化为 `pipeline_latency`、`completion_sync_timestep` 和
`output_time_encoding`；schema 字段名仍以 `.fbs` 为准。

## 在 C 中读取 artifact

`compile_artifacts_generated.h` 是 C++ 绑定，不能直接作为稳定 C ABI 使用。
C 应用包含 [`../artifact_reader.h`](../artifact_reader.h)，由 reader 负责
校验 buffer、对齐和边界，并返回只读 view：

```c
#include "artifact_reader.h"

int inspect_artifact(const uint8_t *data, size_t size) {
    rvrt_artifact_t artifact;
    rvrt_artifact_info_t info;
    rvrt_artifact_runtime_t runtime;
    rvrt_artifact_input_mapping_view_t input_view;
    rvrt_artifact_output_mapping_view_t output_view;
    rvrt_artifact_input_entry_t input_entry;
    rvrt_artifact_output_entry_t output_entry;
    uint32_t config_high;
    uint32_t config_low;
    bool found;

    if (rvrt_artifact_read(data, size, &artifact) != RVRT_ARTIFACT_OK ||
        rvrt_artifact_get_info(&artifact, &info) != RVRT_ARTIFACT_OK ||
        rvrt_artifact_thread_runtime(&artifact, 0, &runtime) != RVRT_ARTIFACT_OK ||
        rvrt_artifact_get_input_mapping_view(&artifact, 0, 0, &input_view) != RVRT_ARTIFACT_OK ||
        rvrt_artifact_get_output_mapping_view(&artifact, 0, 0, &output_view) != RVRT_ARTIFACT_OK ||
        rvrt_artifact_config_frame_words(&artifact, 0, &config_high, &config_low) != RVRT_ARTIFACT_OK) {
        return -1;
    }

    if (rvrt_artifact_input_mapping_entry(&input_view, 0, &input_entry) != RVRT_ARTIFACT_OK ||
        rvrt_artifact_output_mapping_find(&output_view, 0, &output_entry, &found) != RVRT_ARTIFACT_OK) {
        return -1;
    }

    /* info.schema_version, runtime.timesteps, input_entry.addr_axon, ... */
    (void)info;
    (void)runtime;
    (void)config_high;
    (void)config_low;
    (void)found;
    return 0;
}
```

常用读取入口：

- `rvrt_artifact_get_info`：schema version、线程数量和基本统计；
- `rvrt_artifact_thread_runtime`：`timesteps`、`tick_depth`、`sync_steps`、`decode_mode`；
- `rvrt_artifact_get_input_mapping_view` / `get_output_mapping_view`：mapping 元数据和 entry 视图；
- `rvrt_artifact_input_mapping_entry` / `output_mapping_find`：按 entry 下标或 `axon_bit_idx` 读取地址、路由和 dtype；
- `rvrt_artifact_config_word_count` / `config_frame_words`：配置帧数量和规范化后的 word 顺序；
- `rvrt_artifact_thread_count`、`rvrt_artifact_thread_root_core_offset`：线程和根 core 坐标。

稳定 C view 不复制字符串、shape 或 entry 数组；其生命周期受 artifact buffer
约束，调用者必须保持 `.bin` 内存有效。C++ 调试代码才直接调用
`GetCompileArtifacts`、`IoMapping()` 等 generated accessor。

## 解释几个容易混淆的字段

- `shape` 是逻辑 tensor 形状；实际 frame 存储还受 `bit_width`、`tick` 和 entry stride 影响。
- `kind=DATA` 表示离散输出；`kind=VOLTAGE` 表示电压输出，runtime 还会保留电压状态。
  此时 C view 使用 `RVRT_DTYPE_VOLTAGE_INT32` 表示 signed int32；它是 runtime
  语义值，不是序列化的 FBS `DataType` 枚举值。
- `runtime.timesteps` 是模型时间步总数；输出行号仍从 0 开始，不能直接当作硬件累计时间。
- `config_frames.words` 按 `word_order` 描述 32-bit word 的高低半字顺序；优先通过 reader API 读取，不要在应用中自行交换。
- `tick` 是映射的时间槽元数据；它不是输出 timestep，也不是 SYNC payload。

## 相关文档

- [`../README.md`](../README.md)：runtime 架构、session/runner 调用和同步术语。
- [`../artifact_reader.h`](../artifact_reader.h)：稳定 C 读取 API。
- 实验性 executor 与旧 schema 不属于生产 runtime 提交和默认构建。
