# SNN Head UART Demo

This is the complete bare-metal SNN Head application. It receives one
`float32[1][8][768]` tensor over UART, executes
`fc1_lif -> block0 -> block1 -> fc2 -> fc3` through `snn_head_run_chunk()`,
and returns `float32[1][8][7]`.  Complete-chain cycle timing is included when
`SNN_HEAD_TIMING=1`.

The C model API remains rank-2: `float[8][768] -> float[8][7]`. The wire-only
batch dimension is always one.

## Artifact Provenance

The five checked-in assets are byte-identical to the corresponding files
under:

```text
<PAIBOX_EXPORT_ROOT>/snnhead_lif_rdfalse_int8qat_headonly_s1000_q9995_bc_20260701/
artifacts_aer_routing_fixed_20260902_150647/<layer>/runtime/compile_artifacts.bin
```

`<PAIBOX_EXPORT_ROOT>` denotes the directory containing PAIBox model exports;
it is not required by the build.

| Layer | Bytes | SHA-256 |
| --- | ---: | --- |
| `fc1_lif` | 1,310,992 | `1edd33d7fe0f95a44b35f08ac29a3be97e996ed907d6c37cbe9b1335b15e82a9` |
| `block0_lif` | 2,541,968 | `31a130ca4a799e8704e4572267c29322f8e3edbe2c32e2d1ef28067e20947428` |
| `block1_lif` | 2,541,968 | `ed9b3d272baa1187c4dd1d8e3c25381329c639acde0d352f073bd83dd0468b69` |
| `fc2` | 2,535,968 | `76edbdf8ffe445b6206d2f721db5918288b7277e69fe7ed05648ac90c0772258` |
| `fc3` | 85,088 | `d60e156c3a6ed2acb938a8402f3ccc7ddd006500400abeda5a90751356307c1c` |

The local assets are the build inputs, so the PairV build remains
self-contained.

## Source and Build Layout

```text
snn_head/
├── Makefile, main.c          production UART executable
├── include/                  public model, UART, and profile headers
├── src/                      shared model, UART, and profile implementation
├── assets/<layer>/           checked-in PAICore compile_artifacts.bin inputs
├── model.mk                  shared model/runtime/artifact build rules
├── board_test/
│   ├── common.mk, golden/    shared board-test rules and checked-in oracle data
│   └── <target>/             one main.c, one-line Makefile, local generated/
├── float_uart_test/          UART-only executable; no PAICore artifact/runtime
├── cpu_ops_selftest/         CPU-operator executable; no PAICore artifact
└── host_test/                host CMake tests and hardware shims
```

`assets/` is source data and is never written by the build. For each
executable, `model.mk` copies only the selected artifact into that target's
ignored `generated/<layer>/` directory and converts it to a RISC-V object in
`.large_const_data`. The production UART demo and `board_test/chain` select all
five layers; an isolated board test selects only its own layer. `make clean`
removes the complete target-local `generated/` directory so a previous build
configuration cannot leave misleading artifact objects behind.

| Executable | Entry | Shared model/runtime | PAICore assets |
| --- | --- | --- | --- |
| Production UART | `main.c` | complete chain + UART | all five |
| Isolated board layer | `board_test/<layer>/main.c` | selected layer path + profiling | matching layer only |
| Board chain | `board_test/chain/main.c` | complete chain + profiling | all five |
| Float UART test | `float_uart_test/main.c` | UART codec only | none |
| CPU ops selftest | `cpu_ops_selftest/main.c` | CPU kernels only | none |
| Host tests | `host_test/` | host shims/mocks | assets loaded by the test shim |

The Nuclei SDK emits ordinary C/C++ objects beside their source files. Builds
with different `SNN_HEAD_DEBUG` or `SNN_HEAD_TIMING` values therefore must use
`clean all` in this checkout. Parallel variants should use separate worktrees.

Normal builds use the checked-in assets and generated C data. Regeneration
requires PyTorch; board golden generation additionally requires matching
versions of snntorch, PAIBox, and paicorelib from the model-export environment.

## UART Contract

Frames use the existing `NSNN` v1, 48-byte little-endian header. Header CRC
and payload CRC use CRC-32/ISO-HDLC (reflected polynomial `0xedb88320`, init
and xorout `0xffffffff`).

| Frame | Shape | Payload | Meaning |
| --- | --- | ---: | --- |
| INPUT | `[1,8,768]` | 24,576 bytes | IEEE-754 binary32 little-endian input |
| INPUT_ACK | `[1,8,768]` | 0 bytes | Valid input accepted |
| RESULT | `[1,8,7]` | 224 bytes | Final action, `cycles`, and clock frequency |
| ERROR | none | 0 bytes | Header, payload, shape, or inference failure |

The firmware rejects NaN and infinity after decoding and before sending
`INPUT_ACK`. It also returns an inference `ERROR` instead of serializing a
non-finite result. At the start of each transaction it scans for the `NSNN`
magic, allowing the stream to resynchronize after an oversized or interrupted
frame.

With `SNN_HEAD_TIMING=1`, `cycles` measures only the `snn_head_run_chunk()`
call. UART I/O, CRC, float encoding, and RESULT transmission are outside the
interval. With the production default `SNN_HEAD_TIMING=0`, `cycles` is zero;
the clock-frequency field remains populated for protocol compatibility.

`SNN_HEAD_UART_IO_TIMEOUT_MS=1000` limits each UART receive/send phase. At
3,000,000 baud, the 24,576-byte INPUT occupies about 82 ms on the wire. It is
not an inference timeout: every PAICore layer retains its runner timeout, and
the host client allows 90 seconds for the complete transaction. On the board
used for final validation, the complete chain measured 51.608 seconds.

`SNN_HEAD_DEBUG` and `SNN_HEAD_TIMING` select two mutually exclusive,
single-transaction diagnostic images. They are application feature switches,
not GCC/Nuclei SDK build types:

| Debug | Timing | Use |
| ---: | ---: | --- |
| 0 | 0 | Production UART service; no diagnostic instrumentation |
| 0 | 1 | One performance transaction; RESULT followed by timing text, then idle |
| 1 | 0 | One diagnostic transaction; terminal frame followed by one debug line, then idle |
| 1 | 1 | Invalid build configuration |

Production uses both values as zero and stays in the receive loop without text
between binary frames. Diagnostic images accept one terminal
transaction per boot. Runtime ERROR messages are buffered during inference;
debug mode emits one bounded `SNN_HEAD_DEBUG` line only after the binary RESULT
or ERROR. Timing mode similarly emits one bounded-write report after RESULT.
The host treats a missing newline or `SNN_HEAD_TIMING_END` as a truncated log.

The report records INPUT payload receive, float decoding, configuration frame
transmit, input encoding/transmit, INIT round trip, every timestep's
`sync_round_trip` (SYNC through COMPLETE), IRQ service, CPU phases, and RESULT
float encode/transmit. `sync_round_trip` includes PAICore/NoC/result-return
effects and is not a pure PAICore-core counter. Diagnostic output occurs after
the terminal binary frame and outside these reported intervals.

## Build

```sh
source setup.sh
make CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/baremetal/snn_head \
  BANNER=1 SNN_HEAD_DEBUG=0 SNN_HEAD_TIMING=0 clean all
```

The default UART rate is 3,000,000 baud. With `BANNER=1`, the SDK Banner is
followed by `SNN_HEAD_UART_READY`; the board test tool waits for and saves this
marker before sending binary data. `BANNER=0` emits no startup text.

## Host Test

```sh
HOST_BUILD_DIR="$(mktemp -d)"
cmake -S application/baremetal/snn_head/host_test -B "$HOST_BUILD_DIR"
cmake --build "$HOST_BUILD_DIR" -j2
ctest --test-dir "$HOST_BUILD_DIR" --output-on-failure
```

`snn_head_host_uart_test` sends deterministic random input through an
in-memory NSNN endpoint and calls the real complete five-layer chain. It
checks framing, CRC, output shape, timing metadata, and NaN/Inf rejection
without a numerical golden comparison. Negative cases cover header, shape,
length, truncation, stream recovery, CRC, and non-finite tensors. The
choreography test additionally checks fc3 VOLTAGE lane decoding and all 56
dequantized outputs. `snn_head_host_artifact_test` checks all five artifact
contracts and dense-input frame budgets in the same CTest suite.

## CPU Ops Selftest

[`cpu_ops_selftest`](cpu_ops_selftest/README.md) independently checks the real-size
LayerNorm, quantization, FC3, and dequantization CPU kernels against generated
PyTorch references. It runs under N307FD QEMU or on the MCU and does not require
PAICore hardware; the separate `board_test/` targets cover PAICore execution.

```sh
make CORE=n307fd DOWNLOAD=ilm \
  PROGRAM=application/baremetal/snn_head/cpu_ops_selftest clean all run_qemu
```

## Float UART Test

[`float_uart_test`](float_uart_test/README.md) builds a small MCU target around the same
`snn_head_uart.c` used here, without model artifacts or PAICore/runtime code.
Every transaction receives the complete 6,144-float `[1,8,768]` production
input. It returns the first 7 decoded values from each timestep so the host can
verify exact float32 decode/encode, framing, CRC, stream synchronization, and
non-finite tensor rejection independently from model inference.

## Board Transaction

Use the stable board UART identity, not a numbered `ttyUSB` path. Start the
client before upload so it can retain the SDK Banner, then upload from the
same shell:

```sh
RUN_DIR="runs/snn_head_uart_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RUN_DIR"
python3 application/baremetal/snn_head/tools/snn_head_uart_board_test.py \
  --port "<stable-serial-device>" \
  --await-banner --log-dir "$RUN_DIR" &
client_pid=$!
sha256sum application/baremetal/snn_head/snn_head.elf >"$RUN_DIR/elf.sha256"
make CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/baremetal/snn_head \
  OPENOCD="$PWD/openocd-linux-x64-4427ee7/bin/openocd" upload
upload_status=$?
wait "$client_pid"
client_status=$?
exit $((upload_status != 0 ? upload_status : client_status))
```

The client uses fixed seed `20260906`, validates ACK/RESULT/CRC and finite
outputs, and writes `startup.bin`, `input.bin`, `tx.bin`, `rx.bin`, and
`summary.json`. A successful RESULT proves the application returned from the
complete `snn_head_run_chunk()` chain; it does not establish numerical golden
equivalence.

For production continuous-transaction validation, add `--runs N`. The tool
generates `N` complete `[1,8,768]` tensors from seeds `seed..seed+N-1` and saves
their CRCs plus all RESULT CRCs. Do not combine `--runs N` with diagnostic
capture; timing and debug images intentionally stop after one transaction.
Validate repeated complete-chain transactions on the target board before using
the demo as a persistent production service.

## Debug Timing Transaction

Build with `SNN_HEAD_TIMING=1`, then add `--capture-timing-log` to the board
client command above. The run directory also contains `profile.log`, including
the `SNN_HEAD_TIMING_BEGIN` and `SNN_HEAD_TIMING_END` markers, plus Chinese
`timing_report.md`. The MCU emits raw cycle counts only; the host converts
them using the clock frequency returned in RESULT and labels nested runner
metrics so they are not incorrectly added to layer totals:

```sh
make CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/baremetal/snn_head \
  BANNER=1 SNN_HEAD_DEBUG=0 SNN_HEAD_TIMING=1 clean all

python3 application/baremetal/snn_head/tools/snn_head_uart_board_test.py \
  --port "<stable-serial-device>" \
  --await-banner --capture-timing-log --log-dir "$RUN_DIR"
```

For failure diagnosis, build with `SNN_HEAD_DEBUG=1 SNN_HEAD_TIMING=0` and use
`--capture-debug-log`. The one post-response line is saved as `debug.log`.
