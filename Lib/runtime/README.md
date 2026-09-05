# PAIRV Runtime

`Lib/runtime` 是 PAIRV 裸机应用使用 PAIBox 编译产物的轻量运行时。它
负责读取 FlatBuffers artifact、生成 PAICORE 帧、发送输入、处理 IRQ 和
同步、解码输出；模型张量的拥有、预处理、后处理和应用业务仍由上层负责。

这份文档回答三个问题：runtime 如何组织、应用如何调用、术语和时间语义
如何保持一致。产物字段的完整说明见
[`generated/README.md`](generated/README.md)。

## 先看哪条路径

| 需求                          | 入口                 | 说明                                                      |
| ----------------------------- | -------------------- | --------------------------------------------------------- |
| 单线程、固定首层输入输出      | `paicore_runner.h` | `deploy -> run_sample -> release`，适合 demo 和简单应用 |
| 多步输入、显式同步或自定义 RX | `session.h`        | 应用管理 session 生命周期、输入帧和输出解码               |
| 研究中的任务编排实验          | `experimental/`    | 不属于默认 runtime ABI，也不由普通 runtime 构建自动收集   |

不要把 `experimental/artifact_executor.*` 或 `experimental/task_api.h` 当作
当前 `.fbs` 的默认执行计划接口。当前 schema 描述的是模型映射、运行时参数
和配置帧，不描述 CPU task 或通用 execution plan。

## 设计边界

```text
compile_artifacts.bin
        |
        v
artifact_reader  ---- 读取 schema、线程、映射和配置帧
        |
        +--> frame_codec ---- 输入/输出 PAICORE frame 编解码
        |
        +--> session_io ----- UART/IRQ 传输和 RX barrier
        |
        +--> session -------- 手动生命周期、分块输入、同步和解码
        |
        +--> paicore_runner - 单 runner 的 deploy/run/release 便捷封装
```

![PAIRV runtime 的 PAICORE 推理工作流](docs/paicore_runtime_workflow.svg)

| 层             | 负责                                        | 不负责                          |
| -------------- | ------------------------------------------- | ------------------------------- |
| artifact       | 校验和访问`.bin` 的 FlatBuffers 内容      | 分配应用 tensor、执行模型       |
| codec          | 把 tensor/mapping 编成或解成 frame          | 决定模型顺序或业务后处理        |
| session I/O    | 发送 frame、接收 IRQ frame、维护 RX barrier | 阻塞等待或保存业务对象          |
| session        | 初始化、加载配置、分块输入、同步和输出状态  | CPU 算子和模型拓扑编排          |
| PAICORE runner | 固定首线程/首输入/首输出的便捷流程          | 多 runner 调度、通用 task graph |
| application    | tensor 内存、采样循环、预/后处理、错误呈现  | 直接修改硬件同步协议            |

生产代码应沿这条链路调用：
`artifact_reader -> frame_codec -> session -> session_io -> paicore_runner`。
`session` 和 `runner` 是不同对象；不要用含义不清的 `context` 代替它们。

## 最小 runner 示例

```c
#include "paicore_runner.h"

static rvrt_paicore_runner_t runner;

int run_one_sample(const uint8_t *artifact, size_t artifact_size,
                   rvrt_frame_t *frame_buffer, uint32_t frame_capacity,
                   const uint8_t *input, size_t input_bytes,
                   uint8_t *output, size_t output_capacity) {
    const rvrt_paicore_runner_deploy_config_t config = {
        .artifact_data = artifact,
        .artifact_size = artifact_size,
        .frame_buffer = frame_buffer,
        .frame_capacity = frame_capacity,
        .voltage_state = NULL,
        .voltage_state_capacity = 0,
        .timeout_ms = 1000,
    };
    rvrt_session_status_t status = rvrt_paicore_runner_deploy(&runner, &config);
    if (status != RVRT_SESSION_OK) {
        return (int)status;
    }

    status = rvrt_paicore_runner_run_sample(
        &runner, input, input_bytes, 0, output, output_capacity, 0);
    rvrt_paicore_runner_release(&runner);
    return (int)status;
}
```

runner 当前固定使用 artifact 的首线程、首个输入映射和首个输出映射：

- `DATA` 输出按 `uint8_t` 解码；
- `VOLTAGE` 输出按 `int32_t` 解码，并保留状态信息；
- 零 stride 表示紧凑连续布局；
- 同一进程只保持一个 active runner。

需要多输入、多输出、显式 timestep 或自定义同步时，改用手动 session。

## 手动 session 流程

```text
rvrt_artifact_read
  -> rvrt_session_init
  -> rvrt_session_load_config
  -> 每个 sample: reset model -> send input timesteps
  -> rvrt_session_sync_wait_until(completion_sync_timestep, timeout, &rx, &rx_count)
  -> rvrt_decode_output_frames (frame_codec)
  -> rvrt_session_deinit
```

典型调用顺序：

1. 用 `rvrt_artifact_read` 校验对齐、schema version 和 buffer 边界。
2. 读取 thread runtime，按 `timesteps`、`tick_depth` 和映射 stride 准备应用 buffer。
3. `rvrt_session_init` 后加载 artifact 中的 config frames；普通 sample reset 不重复加载配置，只有 PAICORE 重新部署或恢复时才重新加载。
4. 每个 sample 从 timestep 0 开始编码输入并发送；不要复用上一个 sample 的硬件时间。
5. 使用 `rvrt_session_sync_wait_until` 等待累计完成目标，再解码 DATA 或 VOLTAGE 输出。
6. 发生不可恢复错误时调用 `rvrt_session_deinit`，重新初始化 session，不要继续发送帧。

输入和输出 buffer 由应用拥有。session 容量不足返回
`RVRT_SESSION_BUFFER_TOO_SMALL`；
超出映射范围、stride 或 timestep 的请求返回相应错误，不应靠截断继续执行。

## 术语与时间语义

### 对象命名

- **artifact**：FlatBuffers 编码的 `compile_artifacts.bin`。
- **mapping**：输入或输出 tensor 与 PAICORE 地址/位宽/时间的映射。
- **frame**：传输协议中的完整 PAICORE 帧；`frame_buffer` 是其存储区。
- **session**：一次模型交互的状态机；**runner** 是固定首映射的便捷封装。
- **workspace**：codec/session 使用的临时帧空间；不是应用 tensor 所有权。
- **config frame**：模型部署后、sample 输入前发送的配置帧。

### 四种 timestep

| 名称                        | 含义                                      | 典型范围/来源               |
| --------------------------- | ----------------------------------------- | --------------------------- |
| layer-local timestep index  | 当前 tensor 的行号                        | `0 .. T-1`                |
| PAICORE timestep coordinate | 去掉`target_lcn` 地址位后的硬件时间坐标 | 由 frame 地址解出           |
| completed timestep count    | 自最近一次硬件 reset 后已完成的累计步数   | 单调递增计数                |
| timeline target             | SYNC barrier 等待的累计目标               | `sync_wait_until(target)` |

`runtime.completion_sync_timestep` 是完成目标，`runtime.pipeline_latency`
是流水延迟。二者都不是输出行号，也不能用 SYNC payload 代替。

例如目标序列为 `1 -> 2 -> 8` 时，timeline SYNC payload 为
`1 -> 1 -> 6`（相邻目标的 delta）；payload 是协议字段，不是绝对时间。

runner 的 model reset 建立“本地 timestep 0”和硬件时间的对应关系。仅把
软件计数器清零，或跳过 INIT 后重新发送输入，都不能重置硬件状态；不要用
取模或偏移量掩盖 reset 错误。

### SYNC barrier 和 RX handler

一次同步等待的状态序列是：

```text
开始 RX barrier -> 发送 SYNC -> 收到非 COMPLETE 的 IRQ frame
                 -> 收到 COMPLETE -> 结束 barrier -> 阻塞调用返回
```

RX handler 在 IRQ 上下文处理非 COMPLETE frame，只做快速解析、记录状态和
唤醒等待者。handler 不得阻塞、分配内存、调用 session API 或保存 frame 指针；
frame 内容只在回调期间有效。`RVRT_SYNC_MODE_RAW` 和
`RVRT_SYNC_MODE_TIMELINE` 的 payload 解释不同，必须与 artifact runtime
配置一致。

## 构建与验证

应用 Makefile 通常包含：

```make
INCDIRS += . $(NUCLEI_SDK_ROOT)/Lib
include $(NUCLEI_SDK_ROOT)/Lib/runtime/build.mk
```

`Lib/runtime/build.mk` 提供 `RVRT_SESSION_ENABLE_STATS`（默认 `0`）、头文件
路径和 runtime 源目录。实验性 executor 位于 `Lib/runtime/experimental`，
不会因包含该 Makefile 而进入生产构建。

裸机示例可按项目 Makefile 使用：

```sh
source setup.sh
make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/flatbuffers clean all
```

主机 runtime 测试：

```sh
cmake -S tests/runtime -B /tmp/pairv-runtime-build
cmake --build /tmp/pairv-runtime-build
ctest --test-dir /tmp/pairv-runtime-build --output-on-failure
```

主机测试验证 codec、artifact reader、session 控制和 mock I/O；它不等价于
板卡上的 PAICORE 或 UART 证据。需要硬件证据时，另行按板卡测试流程采集启动、
`System ready.` 和阶段完成日志。

## 相关文件

- [`generated/README.md`](generated/README.md)：`.fbs`、`.bin`、生成头文件和 C 读取示例。
- [`artifact_reader.h`](artifact_reader.h)：稳定的 artifact C ABI。
- [`frame_codec.h`](frame_codec.h)：输入/输出 frame 编解码 API。
- [`session.h`](session.h)：手动 session、同步和 RX handler API。
- [`paicore_runner.h`](paicore_runner.h)：单 runner 便捷 API。
