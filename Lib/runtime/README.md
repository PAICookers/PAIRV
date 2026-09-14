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
| 单线程、固定首层输入输出      | `paicore_runner.h` | 支持便捷 deploy 或 prepare/attach 的驻留模型流程         |
| 多步输入、显式同步或自定义 RX | `session.h`        | 应用管理 session 生命周期、输入帧和输出解码              |
| 研究中的任务编排实验          | `experimental/`    | 不属于默认 runtime ABI，也不由普通 runtime 构建自动收集  |

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

runner 存储必须先零初始化。`deploy` 保留原有的一步式行为。多个已配置模型需要
共享一个 IRQ receiver 时，使用以下拆分生命周期：

```text
每个模型一次: prepare
每个模型一次: attach -> load_config -> detach
每个 sample: attach -> INIT/run_sample -> detach
最终释放: release
```

`prepare` 只解析和校验 artifact，不注册 IRQ，也不发送帧。`attach` 借用共享
frame buffer 和可选 coverage bitmap，并独占当前进程的 PAICORE NoC receiver；
同时 attach 第二个 runner 返回 `RVRT_SESSION_BUSY`。成功的 `load_config` 状态在
detach 后保留，因此重新 attach 不会要求或允许重复加载配置。`release` 清除
descriptor；配置加载失败的 descriptor 在 release 前不能重新 attach。

`RVRT_PAICORE_RUNNER_RX_EXACT` 要求每个 timestep 的每个 DATA 元素，或每个
VOLTAGE 元素的四个 byte lane，都恰好出现一次，再加一条 metadata `thread_id`
匹配的 COMPLETE。重复、错误 kind、地址、lane、timestamp 或 thread identity
会使 session fault。缺失帧可能让 CPU 阻塞在 FIFO MMIO read，须由 PC 外部
deadline 和软件 reset 负责恢复。DATA exact 因此要求模型为零值也显式发帧，
例如在确认语义不改变计算结果后启用输出核的 `zero_output`。

当前 runner 的 EXACT 调度只接受 `pipeline_latency == 1`。更深流水需要按每个
SYNC 和最终 flush 分别计算 goal，尚未纳入此接口；普通 SPARSE 调度不受此限制。

EXACT 的 bitmap 大小为 `ceil(T * elements * lanes / 32)` 个 `uint32_t`；当前
SNN Head 最大为 49152 bit，即 6144 byte。规则连续的 output mapping 可使用非零
base 地址并走快速 decoder；有 hole、重复 element 或其他不规则 mapping 时退回
artifact lookup，仍保留相同完整性检查。

输入映射也可以在 `prepare` 的既有全量校验中识别规则布局：signed INT8、
8-bit、`copy_id=0`、最多8个固定多播 cover，条目按 element 外层／cover 内层
排列，`tick_relative=element/64`、`addr_axon=(element%64)*8`，并保持统一的
input `target_lcn` 和各 cover 的路由／复制分量。验证成功后，每个 runner
缓存最多8个完整输入条目原型，sample 直接生成元素地址，继续复用已有 payload
和 WORK1 帧打包。非规则映射退回原通用编码器，仍完成原有 schedule 校验，
保留原来的错误时点、零值跳过、分批 cursor 和帧顺序。手动 session API
继续使用通用路径。

缓存随 runner 在 detach 后保留，release 时清空；它不缓存样本数值，也不替代
artifact 的借用生命周期：PBCA backing bytes 必须一直有效且不可修改。
每个 runner 的原型数组占384 B，五个 runner 共1,920 B，另有标志和计数字段。
`rvrt_paicore_runner_t` 的大小因此增加，应用与 runtime 必须一起完整重编译，
不能复用旧结构尺寸的对象文件；栈上 runner 的调用方还需重新核对栈预算。
EXACT 接收、INIT/SYNC、统计和共享发送缓冲区的行为保持原合同。

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

普通稀疏同步等待的状态序列是：

```text
开始 RX barrier -> 发送 SYNC -> 收到非 COMPLETE 的 IRQ frame
                 -> 收到 COMPLETE -> 结束 barrier -> 阻塞调用返回
```

RX handler 在 IRQ 上下文处理非 COMPLETE frame，只做快速解析、记录状态和
唤醒等待者。handler 不得阻塞、分配内存、调用 session API 或保存 frame 指针；
frame 内容只在回调期间有效。`RVRT_SYNC_MODE_RAW` 和
`RVRT_SYNC_MODE_TIMELINE` 的 payload 解释不同，必须与 artifact runtime
配置一致。

ECLIC 以 level-trigger 注册 PAICORE NoC IRQ，但实板表明 ACK 后非空 FIFO
不会再触发下一次 IRQ。硬件接口也没有 FIFO 计数。EXACT 因此要求
调用者给出当前 barrier 的固定 `rx_goal`，ISR 在首次 IRQ 内按该数量
连续读取。回调逐帧验证类型、地址、唯一 coverage 和 COMPLETE；即使
COMPLETE 先到，也必须读满 `rx_goal` 并满足语义完成才结束。语义错误会
被锁存，已知响应窗口仍会被消费完再 fault session。

FIFO 空时的 MMIO 读可以阻塞 CPU，此时软件 timeout 无法抢占该读操作。
因此 EXACT 的缺帧失败必须由 PC 端 deadline 发现，并用已验证的 software
reset 恢复。普通稀疏 RX 仍以 COMPLETE 结束，不受 `rx_goal` 约束。

诊断代码可通过内部头 `paicore_runner_internal.h` 的
`rvrt_paicore_runner_exchange_exact` 发送一帧请求，使用显式 `rx_goal`
界定原始
64-bit 响应。它不解释 `0xE...` 为 COMPLETE，不改变 timeline epoch；回调错误或
超时会 fault session。该接口属于 runtime 内部诊断能力，不是公共 runner API。

## 构建与验证

应用 Makefile 通常包含：

```make
INCDIRS += . $(NUCLEI_SDK_ROOT)/Lib
include $(NUCLEI_SDK_ROOT)/Lib/runtime/build.mk
```

`Lib/runtime/build.mk` 提供 `RVRT_ENABLE_STATS`（默认 `0`）、头文件
路径和 runtime 源目录。实验性 executor 位于 `Lib/runtime/experimental`，
不会因包含该 Makefile 而进入生产构建。

宏按可见范围命名：`RVRT_ENABLE_STATS` 以及 `frame_codec.h`、
`artifact_reader.h` 中的 `RVRT_*` 是应用可见的构建/API 常量，保持稳定；
`frame_codec_internal.h` 中的 `RVRT_WF_*`、`RVRT_VOLT_*` 只供 runtime
内部共享，`.c` 文件中的 `FC_*` 和 `RUN_*` 只在对应编译单元内使用。
这些内部宏不是公共 ABI。各头文件的 include guard 和
`PAIRV_RUNTIME_BUILD_MK_INCLUDED` 是防重复包含的结构性宏，不属于 API
命名；generated FlatBuffers 的 include guard 和 `experimental/` 的 ABI
宏也不参与该命名约定调整。

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
