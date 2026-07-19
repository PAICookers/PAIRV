# MNIST Runtime 推理示例

本示例展示如何使用 PAIRV runtime 基础 API，在 PAICORE 2.5 上完成一次完整的
MNIST 推理。应用手动管理 session、输入 timestep、同步和输出解码。

模型包含两层卷积和一层全连接。应用连续发送 8 个输入 timestep，解码得到
`uint8_t output[8][10]`，与软件 oracle 完整比较后执行 spike sum，期望分类
结果为数字 7。

## 目录内容

```text
mnist/
├── main.c                 # 手动 runtime 推理流程
├── data.c / data.h        # 可读的测试输入和 8x10 期望输出
├── assets/
│   └── compile_artifacts.bin  # PAIBox 导出的 PAICORE 2.5 artifact
└── Makefile
```

`data.c`以 116 个有效像素索引保存测试图像，并在启动时展开为 8 份 784-byte
UINT1 输入。期望输出也直接保存在 C 数组中，便于检查和替换。artifact 的原生
格式是 FlatBuffers 二进制，因此继续使用 `objcopy`嵌入固件并保持 8-byte 对齐。

## 读取嵌入的 Artifact

[`Makefile`](Makefile) 先将 `assets/compile_artifacts.bin` 复制为
`generated/compile_artifacts.bin`，再通过 `objcopy -I binary` 生成目标文件，并将
数据放入 flash 的 `.large_const_data` 段。这个输入文件名决定了 `objcopy` 导出的
两个链接器符号：

```c
extern const uint8_t _binary_generated_compile_artifacts_bin_start[];
extern const uint8_t _binary_generated_compile_artifacts_bin_size[];
```

`_binary_generated_compile_artifacts_bin_start` 是嵌入数据的首地址，可直接作为
`const uint8_t *` 传入 `rvrt_artifact_read()`。`_binary_generated_compile_artifacts_bin_size`
是绝对链接器符号：它的**地址值**就是二进制长度，并不指向可读取的数组。因此程序
使用下面的转换，不能解引用该符号：

```c
const uint8_t *artifact_data = _binary_generated_compile_artifacts_bin_start;
size_t artifact_size =
    (size_t)(uintptr_t)_binary_generated_compile_artifacts_bin_size;
```

若修改 artifact 的文件名或生成目录，`objcopy` 自动生成的符号名也会变化；需要同步
更新 [`main.c`](main.c) 中的声明和引用。

## 推理流程

应用只需要按以下顺序组合 runtime API：

```text
rvrt_artifact_read()
  -> 读取 thread runtime 和 input/output mapping
  -> rvrt_session_init()
  -> rvrt_session_load_config()
  -> 对每个应用 timestep 调用 rvrt_session_send_input_timestep()
  -> rvrt_session_sync_wait(runtime.sync_steps)
  -> rvrt_decode_output_frames()
  -> 比较 oracle，执行 spike sum 和 argmax
```

其中：

- artifact bytes、RX buffer、输入 workspace 和输出数组均由应用持有。
- `rvrt_session_send_input_timestep()`内部处理 cursor 和分块发送，应用无需操作
  cursor。
- `rvrt_session_sync_wait()`返回本次同步收到的全部原始帧。
- `rvrt_decode_output_frames()`自动忽略 complete、非 DATA、未映射和应用
  timestep 范围外的帧，输出布局固定为 `[application_timestep][element]`。
- runtime 只负责通用协议操作；spike sum、vote 和 argmax 等模型语义由应用实现。

## 快速编译

从 PAIRV 仓库根目录执行：

```sh
export NUCLEI_SDK_ROOT="$PWD"
source setup.sh

make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/runtime/mnist clean all
```

成功后生成：

```text
application/runtime/mnist/mnist.elf
```

该命令只编译固件，不下载、不连接串口，也不运行板上程序。

## 内存容量

应用显式提供两个可覆盖容量：

```make
APP_WORKSPACE_FRAMES ?= 32
APP_RX_FRAMES ?= 512
```

- `APP_WORKSPACE_FRAMES`是输入编码的临时帧数。容量较小时 runtime 会自动分块，
  最小可设为 1，但不能超过 `RVRT_MAX_WORKSPACE_FRAMES`。
- `APP_RX_FRAMES`是一次同步可保存的原始接收帧数；不足时 session 返回 overflow。
- 容量越大只会增加静态内存占用，不会改变模型结果。

可在构建时覆盖：

```sh
make CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/runtime/mnist \
  APP_WORKSPACE_FRAMES=16 APP_RX_FRAMES=512 clean all
```

## 替换为自己的模型

1. 用 PAIBox 导出纯 PAICORE artifact，替换
   `assets/compile_artifacts.bin`。
2. 在 `data.h`中更新应用 timestep、输入字节数和输出元素数。
3. 在 `data.c`中替换测试输入和期望输出；也可以改为应用自己的数据来源。
4. 根据模型最大输出帧数调整 `APP_RX_FRAMES`，根据可用 SRAM 调整
   `APP_WORKSPACE_FRAMES`。
5. 保留 `main.c`中的 artifact、session、send、sync、decode 调用顺序；只替换
   spike sum/argmax 等业务后处理。

首个应用建议使用 `decode_mode=STREAM`和 DATA 输出。本示例会校验 artifact 的
`timesteps`、`sync_steps`、输出类型和 tensor 元素数，资源不匹配时会在访问硬件
前退出。

## Artifact 协议兼容性

当 PAIBox 的 FlatBuffers artifact 协议改变时，不要只替换
`assets/compile_artifacts.bin`。应先更新 PAIRV runtime 的生成 binding、reader 和测试，
再从匹配版本的 PAIBox 重新导出本文件。若模型 I/O 契约变化，还需同步更新本 demo 的
`data.h`、输入/oracle 和后处理。完整升级顺序见
[`Lib/runtime/README.md`](../../../Lib/runtime/README.md#更新-artifact-协议)。

当前 artifact metadata 为：

```text
timesteps=8, tick_depth=3, sync_steps=10, decode_mode=STREAM
input=784 x UINT1, output=10 x UINT1 DATA
```
