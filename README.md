# PAIRV

PAIRV 是面向集成 PAICORE 的嵌入式平台、基于 Nuclei SDK 构建体系的 RISC-V 嵌入式开发项目，提供板级支持、公共库、PAICORE 运行时和参考应用，用于裸机推理与系统软件开发。

本项目包含：

- Nuclei **N307FD** 的命令行构建、下载与调试流程。
- PAICORE 运行时：模型产物读取、会话控制、输入编码、同步和输出解码。
- 裸机、FreeRTOS、RT-Thread 示例，以及启动代码、链接脚本和 NMSIS 集成。
- 面向运行时和应用的主机测试与板级验证入口。

**推荐在 Linux 上开发。** Windows 提供原生 `cmd` 环境配置及基础固件构建入口；应用附带的主机脚本和测试工具请以各应用说明为准。下文从 `helloworld` 开始，先验证工具环境，再连接板卡。

## 开发要求

环境选择参考 [Nuclei SDK 官方 README](https://github.com/Nuclei-Software/nuclei-sdk#requirements) 和[官方快速上手指南](https://doc.nucleisys.com/nuclei_sdk/quickstart.html)，板卡参数以本仓库配置为准。

| 项目 | Linux | Windows |
| --- | --- | --- |
| 主机系统 | 建议 x86-64、Ubuntu 20.04 LTS 或更新版本 | Windows 10/11 x64 |
| 获取源码 | Git | Git for Windows，命令需在 `cmd` 中可用 |
| 构建工具 | GNU Make ≥ 3.82、Bash、基础命令行工具 | Nuclei Windows Build Tools，提供 `make`、`rm` 等工具 |
| 交叉工具链 | Nuclei RISC-V Embedded GNU Toolchain，提供 `riscv64-unknown-elf-gcc`、GDB 等 | 同左，安装 Windows 版本 |
| 板卡下载与调试 | PAICookers 定制 OpenOCD，Linux x64 版本 | PAICookers 定制 OpenOCD，Windows x64 版本；配置对应 USB 调试驱动 |
| 串口终端 | 如 minicom、screen，需支持 3000000 baud | 如 Tera Term，需支持 3000000 baud |

工具可从 [Nuclei 下载中心](https://www.nucleisys.com/download.php)获取。上游当前 README 推荐 **Nuclei Studio ≥ 2025.10**，其中已包含工具链及构建工具，也可以分别安装命令行工具。这里采用 GNU 工具链路线，IDE 本身不是命令行编译的必需项；该版本建议不代表本项目已验证所有工具版本。

运行固件还需要匹配本项目配置的 PAICORE evalsoc 板卡、N307FD CPU/FPGA 镜像、JTAG 调试连接和 UART 连接。仅编译时不需要连接板卡。当前 [SoC 配置](SoC/evalsoc/build.mk)固定 `CORE=n307fd`、`BOARD=nuclei_fpga_eval`，不能仅更换命令行 `CORE` 参数就适配其他 CPU。

### 板卡专用 OpenOCD

PAIRV 板卡的 Flash 支持需要使用 [PAICookers 定制 OpenOCD](https://github.com/PAICookers/riscv-openocd/releases/tag/latest)，下载并解压对应平台的完整工具包：

- [Linux x64：openocd-4427ee7-linux-x64.tar.gz](https://github.com/PAICookers/riscv-openocd/releases/download/latest/openocd-4427ee7-linux-x64.tar.gz)
- [Windows x64：openocd-4427ee7-windows-x64.tar.gz](https://github.com/PAICookers/riscv-openocd/releases/download/latest/openocd-4427ee7-windows-x64.tar.gz)

系统自带、上游或 Nuclei Studio 随附的标准 OpenOCD 不能替代此定制版本进行本板卡下载。下文通过 `OPENOCD=` 显式指定解压后的可执行文件，Linux 使用 `bin/openocd`，Windows 使用 `bin/openocd.exe`。

## 快速上手

### 1. 获取项目

```shell
git clone https://github.com/PAICookers/PAIRV.git
cd PAIRV
```

项目已包含构建所需的 `Build/`、`SoC/`、`NMSIS/` 等目录，无需另行克隆 nuclei-sdk 或把 PAIRV 放入上游 SDK。基础编译使用仓库内的 `NMSIS/`；`third_party/NMSIS` 子模块用于依赖维护，首次编译 `helloworld` 无需初始化它。

建议将项目和工具解压到不含空格、中文的路径，避免 Makefile、GDB 和外部工具对路径的解析差异。

### 2. 配置开发环境

#### Linux：Bash

安装 Git、Make 和串口终端。例如 Ubuntu/Debian：

```shell
sudo apt update
sudo apt install git make minicom
```

解压 Nuclei 工具到自选目录。工具根目录应包含 `gcc/bin/`；使用 Studio 时，该目录通常为其安装目录下的 `NucleiStudio/toolchain/`。

在项目根目录创建或修改 `setup_config.sh`，将下面的占位路径替换为实际工具根目录：

```shell
NUCLEI_TOOL_ROOT=/path/to/nuclei/toolchain
```

在项目根目录、当前 Bash 会话中加载并检查环境：

```shell
source setup.sh
command -v riscv64-unknown-elf-gcc riscv64-unknown-elf-gdb make
riscv64-unknown-elf-gcc --version
make --version
```

需要下载板卡时，另行确认当前用户具有 USB 调试接口和串口访问权限，并按所用调试器配置 udev 规则。

#### Windows：命令提示符（cmd）

解压 Nuclei Windows 工具链和 Windows Build Tools，整理为以下目录结构；也可以直接使用 Studio 中对应的工具根目录：

```text
<工具根目录>/
├── gcc/
│   └── bin/             # riscv64-unknown-elf-gcc.exe、GDB 等
└── build-tools/
    └── bin/             # make.exe、rm.exe 等
```

在项目根目录新建 `setup_config.bat`，按实际安装位置填写，例如：

```bat
set "NUCLEI_TOOL_ROOT=C:\tools\nuclei\toolchain"
```

打开 **cmd**，进入 PAIRV 根目录后执行：

```bat
call setup.bat
where riscv64-unknown-elf-gcc
where riscv64-unknown-elf-gdb
where make
where rm
riscv64-unknown-elf-gcc --version
make --version
```

[`setup.bat`](setup.bat)读取同目录下的本地配置，检查 GCC 和 Windows Build Tools，并将工具目录加入当前终端的 `PATH`。每次打开新终端后都需重新执行。`setup_config.sh` 和 `setup_config.bat` 均为本机配置，已由 Git 忽略。

以下 Windows 命令使用 `cmd` 语法。若从 PowerShell 开始，请先运行 `cmd`，然后在该会话中完成环境配置和构建。Windows 原生主机上的完整交叉编译和 USB 实板下载尚待验证。

### 3. 编译第一个应用

Linux 和 Windows 均在项目根目录执行：

```shell
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld clean
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld all
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld size
```

编译成功后生成 `application/baremetal/helloworld/helloworld.elf`。如需原始二进制文件，再执行：

```shell
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld bin
```

查看项目及应用的构建帮助：

```shell
make help
make PROGRAM=application/baremetal/helloworld help
```

切换应用、`DOWNLOAD`、架构扩展或编译宏时，先使用相同参数执行 `clean`，再执行 `all`。部分公共源文件的目标文件由不同应用共用，请按顺序构建，避免同时执行多个应用的清理与编译。

### 4. 下载并观察输出

连接板卡电源、JTAG 和 UART，确认 CPU/FPGA 镜像与本项目匹配。在串口终端中打开板卡对应端口，设置 **3000000 baud、8N1、无流控**。这是本项目调试 UART0 的默认设置；应用另行配置串口时以应用说明为准。建议在下载前打开串口，以便捕获启动输出。

Linux，在已加载工具环境的 Bash 中设置定制 OpenOCD 路径并下载：

```shell
PAIRV_OPENOCD=/path/to/pairv-openocd/bin/openocd
"$PAIRV_OPENOCD" --version
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld OPENOCD="$PAIRV_OPENOCD" upload
```

Windows，在已执行 `setup.bat` 的 cmd 中设置路径并下载。传给 Makefile 的路径使用正斜杠：

```bat
set "PAIRV_OPENOCD=C:/tools/pairv-openocd/bin/openocd.exe"
"%PAIRV_OPENOCD%" --version
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld OPENOCD="%PAIRV_OPENOCD%" upload
```

路径需替换为实际解压位置。下载与编译必须使用同一应用和 `DOWNLOAD` 配置。`helloworld` 正常运行后，串口输出包含：

```text
Hello World From Nuclei RISC-V Processor!
```

该应用使用 `ilm` 模式，断电后需重新下载。编译成功、下载命令成功和看到固件正常输出是三个不同的检查点；运行 PAICORE 应用时还需按应用说明检查配置、初始化、同步和结果。

### 5. 调试与选择下载模式

需要交互调试时，可以将上面的 `upload` 替换为 `debug`，保留同一 `OPENOCD=`、`PROGRAM` 和 `DOWNLOAD` 参数；进入 GDB 后按需执行 `load`、断点和 `continue`。构建系统也提供 `run_openocd` / `run_gdb` 双终端调试入口。

| `DOWNLOAD` | 用途与限制 |
| --- | --- |
| `ilm` | 程序下载到 ILM/RAM 并运行，断电丢失；适合基础示例。 |
| `ilmflashxip` | 本项目默认模式。代码和普通只读数据位于 ILM，标记为 `.large_const_data*` 的大常量位于 Flash；ILM 中的代码断电后仍需重新下载。 |
| `flashxip` | 程序存放于 Flash 并直接执行，需匹配板卡镜像及启动配置。 |

以应用 Makefile 和说明文档要求为准。例如包含 Flash 大常量的应用通常使用 `ilmflashxip`，不能照搬 `helloworld` 的 `ilm` 参数。对应的[链接脚本](SoC/evalsoc/Board/nuclei_fpga_eval/Source/GCC)定义了实际存放位置。

## 常见问题

- **找不到编译器或 `make`**：重新加载当前终端环境，检查 `NUCLEI_TOOL_ROOT`；Windows 还需检查 `build-tools/bin`，使用 `where` 确认没有选中其他安装。
- **OpenOCD 无法识别 Flash**：确认命令中的 `OPENOCD=` 指向 PAICookers 定制版本，而非系统或 Studio 随附版本。
- **无法连接 JTAG**：检查供电、线缆、板卡镜像以及 USB 权限或驱动；关闭其他占用同一调试接口的 OpenOCD/IDE 会话。
- **串口没有输出或乱码**：确认端口、UART0 接线和波特率；本项目默认值为 3000000，应用可能覆盖它。重新下载时保持串口终端打开。
- **切换配置后行为异常或链接溢出**：使用目标配置先 `clean` 再编译，核对应用要求的下载模式；附加 `V=1` 可查看完整编译命令。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| [`application/`](application) | 裸机、RTOS 与 PAICORE 参考应用。 |
| [`Lib/`](Lib) | 公共工具与 PAICORE 运行时。 |
| [`Build/`](Build) | Nuclei SDK 风格的公共构建规则。 |
| [`SoC/`](SoC) | evalsoc 启动代码、链接脚本、驱动和板级支持。 |
| [`NMSIS/`](NMSIS) | 应用直接使用的 NMSIS 头文件和库。 |
| [`OS/`](OS) | FreeRTOS、RT-Thread 集成。 |
| [`tests/`](tests) | 运行时及应用验证代码。 |
| [`third_party/`](third_party) | 外部依赖源码与版本维护入口。 |

## 进一步阅读

| 主题 | 入口 |
| --- | --- |
| PAICORE 运行时 API | [`Lib/runtime/README.md`](Lib/runtime/README.md) |
| MNIST 运行时推理示例 | [`application/runtime/mnist`](application/runtime/mnist) |
| FlatBuffers 产物读取示例 | [`application/baremetal/flatbuffers`](application/baremetal/flatbuffers) |
| UART–NoC relay | [`application/baremetal/relay/README.md`](application/baremetal/relay/README.md) |
| Nuclei SDK 构建与调试参考 | [官方文档](https://doc.nucleisys.com/nuclei_sdk/) |

修改公共代码或协议时，请同时更新相关应用文档并运行对应的主机测试和交叉编译检查；测试入口以各目录说明为准。问题反馈可提交到[项目 Issues](https://github.com/PAICookers/PAIRV/issues)，附上主机系统、工具版本、完整构建命令与必要日志，并隐去本机隐私信息。

## 许可证

请参阅仓库中的 [LICENSE](LICENSE)；第三方组件遵循其各自许可证。
