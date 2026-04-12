# PAIRV

PAIRV is a CLI-oriented application project for a custom Nuclei `N307FD`-based RISC-V platform.

The active build flow is defined by the local SDK-style tree in:

- `application/`
- `common/`
- `Build/`
- `NMSIS/`
- `OS/`
- `SoC/`
- `tests/`
- `third_party`

Other directories in the repository are not part of the primary CLI build flow.

## What This Project Provides

- A local Nuclei-style build system that can build applications directly from the command line
- Reusable common code for logging and NICE-related helpers
- Baremetal sample applications for bring-up, NICE/custom instruction validation, UART/SNN integration, NMSIS-NN smoke validation, debug logging, and EmbeddedProto flash assets
- FreeRTOS sample applications
- Baremetal test applications under `tests/`
- Local SoC integration for the current platform, including startup code, linker scripts, and board configuration

## Active Build Layout

```text
PAIRV/
├── common/
│   ├── rv_debug.{h,c}
│   └── rv_nice/
├── application/
│   ├── baremetal/
│   │   ├── debug_demo/
│   │   ├── helloworld/
│   │   ├── nice/
│   │   ├── proto_flash/
│   │   ├── simple_nn/
│   │   └── uart/
│   ├── benchmark/
│   │   ├── coremark/
│   │   └── dhrystone/
│   └── freertos/
│       └── demo/
├── Build/
├── NMSIS/
│   ├── Core/Include/
│   ├── NN/Include/
│   ├── Library/NN/GCC/
│   ├── build.mk
│   └── manifest.json
├── OS/
├── SoC/
├── tests/
│   └── utils/
├── third_party/
│   └── EmbeddedProto/
│   └── NMSIS/
├── Makefile
├── setup.sh
└── setup_config.sh
```

## Default Platform Configuration

Current effective defaults:

- `SOC=evalsoc`
- `BOARD=nuclei_fpga_eval`
- `CORE=n307fd`
- `DOWNLOAD=ilmflashxip`

Application-specific notes:

- `application/baremetal/nice` only supports `DOWNLOAD=ilmflashxip` or `DOWNLOAD=flashxip`
- `application/baremetal/uart` only supports `DOWNLOAD=ilmflashxip` or `DOWNLOAD=flashxip`
- `application/baremetal/proto_flash` only supports `DOWNLOAD=ilmflashxip` or `DOWNLOAD=flashxip`
- `application/baremetal/simple_nn` can be built with `DOWNLOAD=ilm`
- lightweight demos such as `helloworld` and `debug_demo` can be built with `DOWNLOAD=ilm`

## Environment Setup

Before building, configure the toolchain path and export the SDK root.

### 1. Configure the toolchain location

Edit `setup_config.sh` and set `NUCLEI_TOOL_ROOT` to your local Nuclei toolchain directory.

Example:

```sh
NUCLEI_TOOL_ROOT=/path/to/nuclei/toolchain
```

### 2. Export the SDK root

From the project root:

```bash
export NUCLEI_SDK_ROOT=$(pwd)
```

### 3. Load the environment

```bash
source setup.sh
```

If you want to build the [EmbeddedProto](https://github.com/Embedded-AMS/EmbeddedProto) demo, initialize the submodule first:

```bash
git submodule update --init --recursive
```

## NMSIS Layout

PAIRV uses a two-layer NMSIS arrangement:

- `NMSIS/`
  - repo-owned default consumption mirror used by ordinary application builds
  - contains the shipped `Core`/`NN` headers, `build.mk`, and the prebuilt `NN` archive for `rv32imafdc`
- `third_party/NMSIS/`
  - pinned upstream source submodule
  - used by developers when tracking upstream changes or rebuilding the shipped prebuilt library

The default build path remains simple:

```bash
export NUCLEI_SDK_ROOT=$(pwd)
source setup.sh
```

That default path uses the top-level `NMSIS/` mirror automatically.

If a developer wants to test directly against the upstream source tree instead of the shipped mirror, they can override:

```bash
make NUCLEI_SDK_NMSIS=$(pwd)/third_party/NMSIS/NMSIS ...
```

To refresh the shipped prebuilt mirror from the upstream submodule:

```bash
export NUCLEI_TOOL_ROOT=/path/to/nuclei/toolchain
./scripts/refresh_nmsis_prebuilt.sh
```

## Top-Level Make Usage

The root `Makefile` is the intended CLI entrypoint.

### Show root help

```bash
make
```

or

```bash
make help
```

### Build one application

```bash
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld all
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/debug_demo all
make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/nice all
make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/uart all
make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/proto_flash all
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/simple_nn all
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/freertos/demo all
```

### Inspect configuration

```bash
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld info
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld showflags
```

### Generate extra outputs

```bash
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld bin
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld dasm
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld size
```

### Clean

```bash
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld clean
make cleanall
```

### Build all applications

```bash
make buildall
```

`buildall` and `cleanall` scan both `application/` and `tests/`.

## Download And Debug

Examples:

```bash
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld upload
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld run_openocd
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld run_gdb
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld debug
```

These commands depend on:

- a valid toolchain environment
- board/OpenOCD configuration under `SoC/`
- linker scripts under `SoC/`

## Main Application Notes

### `application/baremetal/helloworld`

Simple bring-up example for startup, console output, and base platform verification.

### `application/baremetal/debug_demo`

Minimal demo for the shared `common/rv_debug.{h,c}` logging helper.

### `application/baremetal/nice`

NICE/custom instruction example.

Constraints:

- links with `-lm`
- requires `DOWNLOAD=ilmflashxip` or `DOWNLOAD=flashxip`

### `application/baremetal/uart`

Board-integration-heavy application for UART, SNN-related register access, interrupts, FIFO handling, and command processing.

Constraints:

- requires `DOWNLOAD=ilmflashxip` or `DOWNLOAD=flashxip`

### `application/baremetal/proto_flash`

EmbeddedProto-based demo that links a serialized protobuf asset into flash and uses the shared debug helper.

Constraints:

- requires `third_party/EmbeddedProto`
- requires `DOWNLOAD=ilmflashxip` or `DOWNLOAD=flashxip`

### `application/baremetal/simple_nn`

Minimal NMSIS-NN validation application using a deterministic `conv -> relu -> linear` pipeline.

Constraints:

- links `nmsis_nn`
- default verification target for the shipped prebuilt `NN` library
- can be built with `DOWNLOAD=ilm`

### `application/freertos/demo`

Basic FreeRTOS demo for the current platform.
