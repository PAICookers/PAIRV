# SNN Head UART Demo

This is the complete bare-metal SNN Head application. It receives one
`float32[1][8][768]` tensor over UART, executes
`fc1_lif -> block0 -> block1 -> fc2 -> fc3` through `snn_head_run_chunk()`,
and returns `float32[1][8][7]`.  Complete-chain cycle timing is included when
`SNN_HEAD_TIMING=1`.

The C model API remains rank-2: `float[8][768] -> float[8][7]`. The wire-only
batch dimension is always one.

The final three-chip resident implementation with warm input caching and the
4 KiB stack, verified on 2026-09-14, is named **Multi-chip Scheme 1.0
(多片方案 1.0)**. This is a deployment-scheme version, separate from NSNN v1
and the intermediate firmware build labels. Its exact production ELF and
validation scope are recorded under Three-Chip Resident Mode below; default
builds still select the single-chip path. The two schemes share the model API and are selected at build time.

## Artifact Provenance

The five checked-in single-chip assets under `assets/`, selected by the
default `SNN_HEAD_RESIDENT=0`, are byte-identical to the corresponding files
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
├── artifacts/three_chip/     Scheme 1.0 resident assets, manifest, and audit plan
├── model.mk                  shared model/runtime/artifact build rules
├── board_test/
│   ├── common.mk, golden/    shared board-test rules and checked-in oracle data
│   └── <target>/             one main.c, one-line Makefile, local generated/
├── float_uart_test/          UART-only executable; no PAICore artifact/runtime
├── cpu_ops_selftest/         CPU-operator executable; no PAICore artifact
├── host_test/                host CMake tests and hardware shims
└── resident_host_test/       resident lifecycle and readback host tests
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
with different `SNN_HEAD_DEBUG`, `SNN_HEAD_TIMING`, `SNN_HEAD_RESIDENT`,
`SNN_HEAD_AUDIT`, or `SNN_HEAD_ASSET_DIR` values therefore must use `clean all`
in this checkout. This includes switching between single-chip and resident
assets, which share target-local generated filenames. Parallel variants
should use separate worktrees.

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

With the default `SNN_HEAD_RESIDENT=0`, `SNN_HEAD_DEBUG` and `SNN_HEAD_TIMING`
select two mutually exclusive, single-transaction diagnostic images. They are
application feature switches, not GCC/Nuclei SDK build types:

| Debug | Timing | Use |
| ---: | ---: | --- |
| 0 | 0 | Production UART service; no diagnostic instrumentation |
| 0 | 1 | One performance transaction; RESULT followed by timing text, then idle |
| 1 | 0 | One diagnostic transaction; terminal frame followed by one debug line, then idle |
| 1 | 1 | Invalid build configuration |

The default single-chip production mode uses both values as zero and stays in
the receive loop without text between binary frames. Its diagnostic images
accept one terminal transaction per boot. Runtime ERROR messages are buffered during inference;
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
marker before sending binary data. In single-chip mode, `BANNER=0` emits no
startup text. Resident mode also prints initialization and, when enabled,
audit records; its PC runner requires `BANNER=1` for the ready marker.

## Single-Chip and Multi-Chip Schemes

| Build | Single-chip (default) | Multi-chip Scheme 1.0 |
| --- | --- | --- |
| `SNN_HEAD_RESIDENT` | `0` | `1` |
| Assets | `assets/` | `artifacts/three_chip/` |
| Parameter placement | Each layer deploys on chip0 when called | All five layers remain resident across three chips |
| Initialization | Per-layer deployment during each chunk | Call `snn_head_initialize()` once before requests |
| Chunk API | `snn_head_run_chunk()` | Same API, with per-layer INIT and no parameter reload |
| Receiver | Existing sparse path | Exact unique output/lane coverage plus matching COMPLETE |

Both schemes keep the same eight-step network order and CPU arithmetic.
Scheme 1.0 is a deployment version, not a change to NSNN v1 or PBCA schema.
The historical single-chip output is not a stable numerical oracle for the
multi-chip layout; model golden remains an informational reference.

### Multi-Chip Placement and Execution

| Chip | Resident layers | Model cores | Configured cores |
| --- | --- | ---: | ---: |
| chip0 | FC1+LIF, FC2, FC3 | 58 | 61 |
| chip1 | Block0+LIF | 38 | 41 |
| chip2 | Block1+LIF | 38 | 41 |
| chip3 | None | 0 | 0 |

The five nonoverlapping hardware groups use thread IDs 1–5, with 134 model
cores and 143 configured cores. CPU0 performs all four LayerNorm operations,
quantization/dequantization, and sequential orchestration. Each layer returns
its results to CPU0; CPU1/2/3 do not run model-side operators.

Startup prepares five runner descriptors, then loads each configuration once.
A warm chunk attaches the shared receiver to each layer in turn, issues that
layer's INIT, completes t0–t7, and detaches. The next layer begins only after
all eight steps of its predecessor. This is not a cross-layer pipeline.
No configuration or weight reload occurs between layers or requests.

`RVRT_PAICORE_RUNNER_RX_EXACT` requires every unique DATA output, including
explicit zeros, or all four byte lanes of every VOLTAGE output, plus the
matching thread's COMPLETE. COMPLETE may arrive first but cannot advance the
step before output coverage is complete. The current assets have
`pipeline_latency=1`; each barrier supplies a known receive-frame count.
One IRQ consumes that count while latching semantic errors. Missing frames
can stall a FIFO MMIO read, which the CPU timer cannot preempt: platform
integration must retain an external deadline and recovery mechanism.

The warm input cache recognizes the five assets during `prepare`, storing
at most eight complete mapping prototypes per runner. It reconstructs
element/tick/axon fields and reuses the existing payload/WORK1 packers.
Noncanonical mappings use the generic path. The five descriptors add
1,960 bytes of static RAM, including metadata and alignment; detach retains
the cache, while release or failed prepare clears it. Artifact storage is
borrowed and must remain valid and immutable. Both session and runner public
structures have grown: rebuild all consumers rather than linking old objects.

### C Module Integration

Include `include/snn_head.h` and link the sources/assets selected by `model.mk`.
A consuming PAIRV application defines `NUCLEI_SDK_ROOT` as usual, then uses:

```make
SNN_HEAD_DIR := $(NUCLEI_SDK_ROOT)/application/baremetal/snn_head
SNN_HEAD_RESIDENT := 1
SNN_HEAD_AUDIT := 1
SNN_HEAD_TIMING := 1
SNN_HEAD_DEBUG := 0
include $(SNN_HEAD_DIR)/model.mk
```

Keep the consumer's own entry point; `model.mk` does not add the demo `main.c`
or UART source. Establish the normal platform/NoC IRQ and Flash mappings,
then call `snn_head_initialize()` once. Successful initialization is idempotent.
For every request, provide separate caller-owned, float-aligned buffers for
6,144 finite input floats and 56 output floats and call
`snn_head_run_chunk(input, action)`. Use output only after success, and check
finite output when bypassing the UART wrapper. The direct C API does not
validate input finiteness for the caller.

The module owns shared tensor/frame/voltage-lane buffers, and the runtime has
one active session. Calls must be serialized; this is neither a reentrant nor
a multi-instance API. There is no public single-step, deinit, or retry/reset
API. Initialization, inference, or residency-audit failure stops further
inference; recovery requires platform recovery and a fresh application start.

### Build and Board Prerequisites

The checked-in five assets and audit table make firmware builds self-contained;
Python model-export packages are not needed for a normal build. Asset hashes,
source lineage, fixed coordinates, and historical verification scope are in
[`artifacts/three_chip/README.md`](artifacts/three_chip/README.md).

Use the application-specific linker below for the verified memory contract.
It reserves 4 KiB of stack with SP at `0x90020000`. The prior 2 KiB reservation
was below the reviewed FC3/UART/exact-IRQ call chain (2,112 bytes before the
cache, 2,128 after it), excluding other nested interrupts. Reassess stack
requirements when embedding this module into a larger application; the SDK's
global linker defaults are unchanged.

`model.mk` also sets `SNN_HEAD_STACK_SIZE=4096` for both schemes, since the
single-chip path uses the enlarged runner on its stack. The reviewed
single-chip timing call chain needs at least 2,448 bytes including `main`,
before any additional nesting; leaving it at the SDK's 2 KiB default is
insufficient. Consumers may raise `SNN_HEAD_STACK_SIZE` for their own call
chains. This application-scoped reservation does not change the model's
single-chip asset selection, arithmetic or protocol.

```sh
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/baremetal/snn_head \
  LINKER_SCRIPT="$PWD/application/baremetal/snn_head/linker/ilmflashxip-snn.ld" \
  BANNER=1 SNN_HEAD_DEBUG=0 SNN_HEAD_TIMING=1 \
  SNN_HEAD_RESIDENT=1 SNN_HEAD_AUDIT=1 clean all
```

Do not infer board readiness from a successful build. The recorded validation
used the existing fixed 102-byte/816-bit single-lane0 SOC configuration at
9600 baud, followed by nine short/long communication checks before every
software start. SOC write completion is not a chip ACK. Platform tooling must
apply the matching board configuration, verify communication, and inspect
ELF Flash LOAD ranges before programming. Do not replace this procedure with
an arbitrary delay or assume a previous manual reset configured the lanes.

The machine-specific Flash backup/program/restore, SOC, JTAG and communication
probe harness is not distributed as a portable CLI in this application.
Deploy through the board owner's platform tooling, preserve Flash/boot/QSPI
state as appropriate, and serialize board access. Exact RX complements the
lane0 configuration; it is not a substitute for configuration-packet ordering.
The fixed pureXY return path for chip2 has board evidence, but the generator's
conservative route-policy findings are preserved in the manifest; arbitrary
routing and generic CPU forwarding are not implied.

### Resident Audit and PC Protocol

`SNN_HEAD_AUDIT=1` requires resident mode. Before the first INIT/READY, firmware
compares 143 CORE configurations plus 8,465,120 bytes of parameter SRAM against
the same ELF's PBCA payloads: 8,468,552 bytes in total across 8,450 windows.
After each successful chunk it compares 8,268,288 bytes of immutable weights
across 8,158 windows, excluding mutable neuron state. The audit plan must
match all five assets in the ELF. Do not compare a different build's plan.

With timing and audit enabled, the service accepts consecutive transactions.
Before sending INPUT, validate the initialization, audit-plan identity and
`before_init` audit, then wait for `SNN_HEAD_UART_READY`. After RESULT, consume
through `SNN_HEAD_TIMING_END\n` and validate the
`phase=after_chunk_weights` record before sending another INPUT. A valid
RESULT alone does not establish audit success; that audit runs afterward.
Inference failure sends ERROR then stops the resident service.

The existing `tools/snn_head_uart_board_test.py` provides NSNN `input_frame()`
and `transaction()` helpers. Its diagnostic CLI is not a resident service
client. The pure host module
[`tools/snn_head_resident_protocol.py`](tools/snn_head_resident_protocol.py)
provides startup, audit and resident timing validators without importing board
control tools. Callers still own serial transport, deadlines, log collection,
request serialization, and platform recovery. Do not issue concurrent requests.

UART defaults to 3,000,000 baud. Firmware uses a 1,000 ms UART phase deadline;
runner INIT/SYNC waits each use 2,000 ms, not an entire-layer deadline.
The recorded platform harness allowed 180 seconds for startup and individual
transaction/report reads, since initialization includes full readback.

### Recorded Scheme 1.0 Validation

The final cache plus 4 KiB stack production ELF verified on 2026-09-14 had
SHA-256 `7573e4dda030e1b67826507243d648a097cc07b451b38c68fa251275ad0bbfd6`.
Three software starts, each with fresh SOC/communication checks and A/B/A,
completed nine warm requests averaging **0.405621189 seconds** (range
0.405591929–0.405641473). This is **33.7247 times** faster than the pre-cache
resident mean of 13.679457959 seconds. All A/B outputs matched their
corresponding pre-cache board outputs byte-for-byte. Startup CORE/parameter
and post-request weight audits passed; all 45 layer records had zero
configuration/deploy counters and nine COMPLETEs per layer.

Initialization averaged 119.002554 seconds including the before-INIT audit.
UART, startup, and post-request weight audits are outside the warm interval;
the warm figure includes CPU preprocessing, encoding, NoC, IRQ and PAICORE
execution. It is not pure neural-core time or end-to-end UART service latency.

Separate delayed-capture runs preserved all 19 stage CRCs at eight timesteps,
three full spike tensors and all 56 action bit patterns versus the pre-cache
three-chip diagnostic. CRC equality alone is screening for tensors not
retained in full. The existing model-reference action differences remain;
old single-chip output variation has no uniquely established root cause.
Repeated physical cold boots, long-duration reliability, arbitrary layouts,
and a robot/frontend closed loop remain outside this evidence. New builds
need their own identity and validation; the recorded ELF hash does not label
an arbitrary recompile as board-tested.

### Resident Host Tests

These tests run on a Linux host without board access, using ASan/UBSan:

```sh
cmake -S application/baremetal/snn_head/resident_host_test \
  -B /tmp/snn-resident-build -DSNN_HEAD_AUDIT=ON
cmake --build /tmp/snn-resident-build -j2
ctest --test-dir /tmp/snn-resident-build --output-on-failure
```

The suite covers repeated chunks, fail-stop lifecycle, corrupted audit data,
and formatting/host parsing of startup and audit records. It defaults to the
three-chip assets. Host mocks are not board numerical or timing evidence.

### Maintenance

When model parameters, placement, interfaces, runtime behavior, memory layout,
or performance change, update this guide and the artifact identity/validation
record in the same change. Keep scheme versions separate from wire/schema
versions. Record compiler flags, asset and ELF identities, test conditions,
and whether evidence is host-only or board-verified. Preserve the default
single-chip contract unless a change explicitly documents its migration.

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
capture in the default single-chip mode; those timing and debug images
intentionally stop after one transaction. Resident timing/audit transactions
use the dedicated runner described above.
Validate repeated complete-chain transactions on the target board before using
the demo as a persistent production service.

## Debug Timing Transaction

For the default single-chip mode, build with `SNN_HEAD_TIMING=1`, then add
`--capture-timing-log` to the board client command above. The run directory
also contains `profile.log`, including
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
