# PAIRV

PAIRV is a Nuclei RISC-V SDK-style project for the PAICORE-enabled evalsoc
platform. It provides the board integration, shared libraries, build system,
and reference applications needed to develop and validate bare-metal inference
and system software.

## What It Provides

- A command-line build flow for the Nuclei `N307FD` platform.
- PAICORE runtime APIs for artifact loading, manual session control, input
  encoding, synchronization, and output decoding.
- Bare-metal, FreeRTOS, and RT-Thread sample applications.
- SoC startup code, linker scripts, board support, NMSIS integration, and
  focused runtime tests.

## Quick Start

Install the Nuclei RISC-V GNU toolchain, set its root in
[`setup_config.sh`](setup_config.sh), then load the environment:

```sh
source setup.sh
```

Build a basic board application from the repository root:

```sh
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld all
```

Use `make help` to inspect the available top-level commands. Application
Makefiles define any additional target or download-mode requirements.

## Repository Layout

| Path                           | Purpose                                                           |
| ------------------------------ | ----------------------------------------------------------------- |
| [`application/`](application) | Reference applications and benchmarks.                            |
| [`Lib/`](Lib)                 | Shared PAIRV utilities and PAICORE runtime library.               |
| [`Build/`](Build)             | Common Nuclei SDK-style build rules.                              |
| [`SoC/`](SoC)                 | evalsoc startup code, linker scripts, drivers, and board support. |
| [`NMSIS/`](NMSIS)             | NMSIS headers and libraries consumed by applications.             |
| [`OS/`](OS)                   | FreeRTOS and RT-Thread integration.                               |
| [`tests/`](tests)             | Bare-metal and host-side validation.                              |
| [`third_party/`](third_party) | Pinned external dependencies.                                     |

## Further Reading

| Topic                               | Entry point                                                               |
| ----------------------------------- | ------------------------------------------------------------------------- |
| Manual PAICORE inference API        | [`Lib/runtime/README.md`](Lib/runtime/README.md)                         |
| MNIST runtime inference example     | [`application/runtime/mnist`](application/runtime/mnist)                 |
| FlatBuffers artifact reader example | [`application/baremetal/flatbuffers`](application/baremetal/flatbuffers) |
| Other board and OS examples         | [`application/`](application)                                            |

## License

This project is distributed under the [LICENSE](LICENSE) file in this
repository.
