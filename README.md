# PAIRV

PAIRV is a CLI-oriented application project for a custom Nuclei `N307FD`-based RISC-V platform.

The active build flow is defined by the local SDK-style tree in:

- `application/`
- `Build/`
- `NMSIS/`
- `OS/`
- `SoC/`

Other directories in the repository are not part of the primary CLI build flow.

## What This Project Provides

- A local Nuclei-style build system that can build applications directly from the command line
- Baremetal sample applications for bring-up, NICE/custom instruction validation, and UART/SNN integration
- FreeRTOS sample applications
- Local SoC integration for the current platform, including startup code, linker scripts, and board configuration

## Active Build Layout

```text
PAIRV/
├── application/
│   ├── baremetal/
│   │   ├── helloworld/
│   │   ├── nice/
│   │   └── uart/
│   └── freertos/
│       └── demo/
├── Build/
├── NMSIS/
├── OS/
├── SoC/
├── Makefile
├── setup.sh
└── setup_config.sh
```

## Default Platform Configuration

Current effective defaults:

- `SOC=evalsoc`
- `BOARD=nuclei_fpga_eval`
- `CORE=n307fd`
- `DOWNLOAD=ilm`

Application-specific exception:

- `application/baremetal/nice` only supports `DOWNLOAD=ilmflashxip`

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

Typical setup sequence:

```bash
export NUCLEI_SDK_ROOT=$(pwd)
source setup.sh
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
make PROGRAM=application/baremetal/helloworld all
make PROGRAM=application/baremetal/nice all
make PROGRAM=application/baremetal/uart all
make PROGRAM=application/freertos/demo all
```

### Build with an explicit download mode

```bash
make PROGRAM=application/baremetal/helloworld DOWNLOAD=ilm all
make PROGRAM=application/baremetal/nice DOWNLOAD=ilmflashxip all
make PROGRAM=application/baremetal/uart DOWNLOAD=flashxip all
```

### Inspect configuration

```bash
make PROGRAM=application/baremetal/helloworld info
make PROGRAM=application/baremetal/helloworld showflags
```

### Generate extra outputs

```bash
make PROGRAM=application/baremetal/helloworld bin
make PROGRAM=application/baremetal/helloworld dasm
make PROGRAM=application/baremetal/helloworld size
```

### Clean

```bash
make PROGRAM=application/baremetal/helloworld clean
make cleanall
```

### Build all applications

```bash
make buildall
```

## Download And Debug

Examples:

```bash
make PROGRAM=application/baremetal/helloworld upload
make PROGRAM=application/baremetal/helloworld run_openocd
make PROGRAM=application/baremetal/helloworld run_gdb
make PROGRAM=application/baremetal/helloworld debug
```

These commands depend on:

- a valid toolchain environment
- board/OpenOCD configuration under `SoC/`
- linker scripts under `SoC/`

## Main Application Notes

### `application/baremetal/helloworld`

Simple bring-up example for startup, console output, and base platform verification.

### `application/baremetal/nice`

NICE/custom instruction example.

Constraints:

- links with `-lm`
- requires `DOWNLOAD=ilmflashxip`

### `application/baremetal/uart`

Board-integration-heavy application for UART, SNN-related register access, interrupts, FIFO handling, and command processing.

### `application/freertos/demo`

Basic FreeRTOS demo for the current platform.
