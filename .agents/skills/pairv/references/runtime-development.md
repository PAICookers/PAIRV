# C runtime 开发

按改动读取下列相关章节、[runtime 指南](../../../../Lib/runtime/README.md) 和公共头。修改公共符号前核对调用方、头文件、测试和构建；模型形状、前后处理与报告留在应用。`application/runtime/mnist/` 可作为 session/runner 示例。索引可用时按当前 checkout 选择项目；不可用或过期时直接搜索源码，不依赖某个固定插件。

纯 Host 回归不访问板卡；需要交叉构建或实板验证时，再读[构建与板卡运行](board-build-flash.md)。

## 生命周期与模块

生产调用链：

```text
rvrt_runtime_session_open -> rvrt_runtime_session_configure
  -> rvrt_thread_runner_open -> rvrt_thread_runner_run_sample
  -> rvrt_thread_runner_close -> rvrt_runtime_session_close
```

- session 验证并借用 artifact，管理共享 transport；configure 幂等。
- runner open 选择 thread/mapping，不发送配置、INIT 或 SYNC；每个 run_sample 独立 INIT。
- 同一 session 可挂多个 runner，硬件操作串行且仅一个 RX barrier 活跃。活动 runner/barrier 未释放时 close 返回 BUSY，统计归共享 session。
- timeout、overflow、硬件或 RX handler 失败后保留共享 sticky FAULTED 和清理顺序。平台恢复后再新建 session，不通过自动重试绕过故障态。
- `transport*` 处理 NoC/IRQ/barrier，仅作库内部实现和专门回归。不要把内部结构暴露成应用的同步信号，或让业务处理进入 transport。
- reader、codec、runtime 分别使用 `rvrt_artifact_status_t`、`rvrt_codec_status_t`、`rvrt_runtime_status_t`；不要无依据合并错误域。

## Buffer 与所有权

- artifact bytes 由调用方持有，满足 reader 的对齐要求；session/view 存活期间不得移动或释放。字符串、mapping view 和内部指针都是借用。
- frame workspace、输入、输出、VOLTAGE state 均由调用方提供。保留实际代码中的 capacity、stride、overlap、整数溢出与范围校验；不引入隐藏大 buffer 或动态分配。
- `size_t` 表示内存字节容量；`uint32_t` 表示帧数、索引和协议字段。`rvrt_frame_t` 的 high/low 是逻辑字，不是 serialized 存储或实际 FIFO 顺序的同义词。
- DATA 完整窗口按 `[timestep][element]` 存储；VOLTAGE 由四个字节 lane 重组 int32。runner 的 VOLTAGE state 容量为 `T × element_count`，每个样本初始化。

## 时间与解码

- `runtime.timesteps` 是应用序列长度；serialized `tick_depth`、`sync_steps`、`decode_mode` 对应 C metadata 的 `pipeline_latency`、`completion_sync_timestep`、`output_time_encoding`。使用产物的完成目标，不另推层数或延迟公式。
- 当前 C ThreadRunner 接受 STREAM 调度；不要把 Host Python 支持的其他模式写成 C runner 已支持。扩展前先证明协议时间语义并补回归。
- INIT 的 COMPLETE 建立样本本地时间基准。每输入 timestep 后独立推进并等 COMPLETE，再发送下一步；产物要求的 tail barrier 由 runner 处理。
- 按当前 frame 时间戳与 `target_lcn` 地址规则解码，不重复减流水线延迟。不从时间戳值域臆测可积压的输入深度，也不把跳 INIT 的 raw 实验混作普通 runner。
- 解码只负责数据路由与 lane 组装；spike sum、投票、argmax 和 oracle 比较属于应用。
- 规范布局在设置阶段验证后可缓存 O(1) 快路径；其余合法布局保留通用 decoder。快/慢路径都需容忍合法帧交错；不为计时引入 IRQ 日志、分配或 session 重入。

## 构建与验证

`Lib/runtime/build.mk` 自动加入 runtime C/C++ 源目录与 include。应用另外提供产物资源和 FlatBuffers include；不要手工枚举已经删除的旧源文件。

`RVRT_ENABLE_STATS=0|1` 是当前统计宏，日志另用 `RV_DEBUG_ENABLE_LOGGING`。通过 Make 变量覆盖，不叠加冲突的 `-D`。统计 snapshot 稳定不代表 session/transport 布局稳定；所有 C/C++ 单元、应用和库须用一致宏值重建。

按改动选择回归：reader 的合法/非法元数据与借用，codec 的分块/全零/边界/四 lane，runner 的 mapping 隔离、生命周期、barrier、FAULTED，以及解码快/慢路径一致性。使用新的构建目录验证：

```bash
cmake -S tests/runtime -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --config Debug
ctest --test-dir "$BUILD_DIR" -C Debug --output-on-failure
```

`BUILD_DIR` 由调用方选择。Windows 多配置 generator 使用匹配配置，Host 编译器与交叉编译器分开；环境准备见 [项目 README](../../../../README.md)。

公共 API 保持 C/C++ 兼容；注释交代所有权、单位、状态前提和返回值，内部注释只解释硬件约束或非显然逻辑。检查修改的 C/C++ 格式与 `git diff --check`。新协议/宏/ABI 变更还需核对所有生产调用方、测试和绑定生成说明。
