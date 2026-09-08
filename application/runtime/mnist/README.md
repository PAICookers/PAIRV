# MNIST Runtime 示例

本示例演示如何在 PAIRV 上部署一个 PAICORE artifact，输入两个 MNIST 样本并检查
推理结果。模型接收 8 个时间步的 `784 x UINT1` 输入，输出 `8 x 10` 个脉冲；两个
内置样本的期望分类分别为 7 和 2。

## 运行

以下命令均从 PAIRV 仓库根目录执行。

### 1. 编译

```sh
export NUCLEI_SDK_ROOT="$PWD"
source setup.sh

make CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/runtime/mnist clean all
```

生成的固件位于：

```text
application/runtime/mnist/mnist.elf
```

### 2. 监听串口

先找到稳定的串口设备名：

```sh
ls -l /dev/serial/by-id/
```

在下载固件前启动 3000000 baud 串口监听：

```sh
python3 -m serial.tools.miniterm \
  /dev/serial/by-id/<FTDI-debug-UART> 3000000
```

### 3. 下载并运行

在另一个终端执行：

```sh
export NUCLEI_SDK_ROOT="$PWD"
source setup.sh

make CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/runtime/mnist \
  OPENOCD="$PWD/openocd-linux-x64-4427ee7/bin/openocd" upload
```

`OPENOCD` 必须指向适配 PAIRV 板卡的定制版本。源码见
[`PAICookers/riscv-openocd`](https://github.com/PAICookers/riscv-openocd)；
[`latest` release](https://github.com/PAICookers/riscv-openocd/releases/tag/latest)
同时提供 Linux x64 和 Windows x64 预编译包，解压后分别使用 `bin/openocd` 或
`bin/openocd.exe`。

## 判断是否正确

成功时，串口输出包含：

```text
mnist: sample=0 prediction=7 PASS
mnist: sample=1 prediction=2 PASS
mnist: samples=2 decoded=8x10 MNIST_PASS
```

必须看到最终的 `MNIST_PASS` 才表示推理通过。OpenOCD 下载成功只说明下载步骤完成，
不能证明固件启动或推理正确。

常见失败输出：

| 输出                            | 含义                                          |
| ------------------------------- | --------------------------------------------- |
| `artifact contract mismatch`  | artifact 的时间步、输入或输出与应用常量不匹配 |
| `session ... failed`          | 配置、输入发送、同步或接收阶段失败            |
| `output mismatch`             | PAICORE 的逐时间步输出与内置期望输出不同      |
| `prediction=... expected=...` | spike sum 后的分类结果不正确                  |

如果串口没有出现 `mnist:` 开头的内容，先检查串口设备、波特率，以及监听程序是否在
下载前启动。

## 示例如何工作

[`main.c`](main.c) 使用手工 session API 执行完整输出窗口：

```text
读取 artifact 并检查模型 I/O
  -> 初始化 session 并加载 PAICORE 配置
  -> 每个样本开始前 reset
  -> 发送全部 8 个输入时间步
  -> 同步到 artifact 的完成时间点
  -> 解码为 [8][10] 输出
  -> 比对期望输出，执行 spike sum 和 argmax
```

runtime 负责 artifact、配置、输入帧、同步和输出解码。输入构造、结果校验、spike sum
与 argmax 属于应用逻辑。

## 定制自己的应用

可以复制本目录作为新应用，然后按以下顺序修改。

1. 将 PAIBox 导出的 artifact 放到 `assets/compile_artifacts.bin`。Makefile 会把它嵌入
   固件，无需手工转换。
2. 在 `data.h` 中修改时间步数、每步输入字节数和输出元素数，并在 `data.c` 中提供
   输入数据。没有 golden 数据时，可以移除 `memcmp()`，换成自己的结果检查。
3. 保留 `main.c` 中的 artifact 读取、session 初始化、加载配置、reset、发送、同步和
   解码顺序；替换输入准备及后处理逻辑。
4. 按一次同步可能收到的最大帧数设置 `RVRT_APP_RX_FRAMES`。默认 DATA 输出可按
   `时间步数 x 输出元素数 + 1 个 complete frame` 估算。
5. 构建新应用时包含 runtime 构建文件，无需声明 runtime 目录或逐个列出源码：

```make
include $(NUCLEI_SDK_ROOT)/Lib/runtime/build.mk
```

本示例要求 artifact 使用 STREAM DATA 输出，并在访问硬件前检查 artifact 与应用数组
尺寸是否一致。需要 VOLTAGE 输出、样本级 runner 或更底层的同步控制时，参阅
[`Lib/runtime/README.md`](../../../Lib/runtime/README.md)。

## 文件索引

| 文件                                      | 用途                                  |
| ----------------------------------------- | ------------------------------------- |
| [`main.c`](main.c)                       | runtime 推理流程和 MNIST 后处理       |
| [`data.c`](data.c) / [`data.h`](data.h) | 两个输入样本、期望输出和标签          |
| `assets/compile_artifacts.bin`          | PAIBox 导出的 PAICORE artifact        |
| [`Makefile`](Makefile)                   | artifact 嵌入、runtime 接入和固件构建 |
