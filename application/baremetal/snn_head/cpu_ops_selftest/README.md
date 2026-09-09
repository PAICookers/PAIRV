# SNN Head CPU Ops Selftest

This bare-metal app verifies the SNN Head CPU-side kernels on the N307FD RISC-V target using real dimensions and exported INT8-QAT parameters.

It is the CPU-only companion test for the SNN Head application. It supports
QEMU smoke testing and real-board validation without running the PAICore
runtime pipeline.

## What It Tests

The app runs the following kernels from `PAIRV/Lib/`:

- `rv_layernorm_f32`: fp32 LayerNorm, PyTorch-compatible
- `rv_quantize_s8`: fp32 to int8 symmetric quantization
- `rv_fc_s8_s32`: int8 Linear / FC to int32 accumulator
- `rv_dequantize_s32`: int32 accumulator to fp32 output

The real-size cases are:

| Test | Shape / Size | Data source |
|---|---:|---|
| LN1 | 768 | real `model.layer_norm1.weight_float/bias_float` |
| LN2 | 1536 | real `model.layer_norm2.weight_float/bias_float` |
| Quantize | 1536 | real `model.fc3.activation_scale` |
| FC3 | 1536 -> 7 | real `model.fc3.weight_int8/bias_int32` |
| Dequant/action | 7 | real `model.fc3.output_scale` |

This is not a full end-to-end SNN Head or PAICore NoC runtime test. It only validates the CPU-side kernels in a RISC-V bare-metal ELF.

## Data Source

`test_data.h` is generated from an INT8-QAT export state. Supply its path with
`--export-state`:

```bash
python3 gen_test_data.py \
  --export-state "<export-state.pt>" \
  --out test_data.h
```

When `--export-state` is omitted, the script checks the conventional sibling
PAIBox export layout. The script requires PyTorch; normal builds use the
checked-in `test_data.h` and do not run it. The script emits:

- real LayerNorm weights and biases
- real FC3 int8 weights and int32 bias
- real activation/output scales
- deterministic test inputs
- PyTorch reference outputs

If `test_data.h` is regenerated, commit it only if the repository is allowed to contain exported model parameters.

## Build and Run on QEMU

From the PAIRV root:

```bash
export NUCLEI_SDK_ROOT="$PWD"
source setup.sh
```

Generate test data:

```bash
python3 application/baremetal/snn_head/cpu_ops_selftest/gen_test_data.py \
  --export-state "<export-state.pt>"
```

Build and run:

```bash
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilm \
  PROGRAM=application/baremetal/snn_head/cpu_ops_selftest clean all
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilm \
  PROGRAM=application/baremetal/snn_head/cpu_ops_selftest run_qemu
```

Expected output:

```text
SNN Head CPU ops selftest (N307FD/QEMU)
Package dims: LN1=768 LN2=1536 fc3=1536->7
[PASS] LN1 ...
[PASS] LN2 ...
[PASS] quant ... mismatches=0
[PASS] fc3 ... mismatches=0
[PASS] action dequant ...
RESULT: ALL PASS
```

A known passing QEMU result is:

```text
[PASS] LN1 dim=768 max_abs_diff=715 e-9 finite=1
[PASS] LN2 dim=1536 max_abs_diff=5125 e-9 finite=1
[PASS] quant dim=1536 scale=28422194 e-9 mismatches=0
[PASS] fc3 rows=1 in=1536 out=7 macs=10752 mismatches=0
[PASS] action dequant out=7 max_abs_diff=0 e-9 finite=1
RESULT: ALL PASS
```

## Run on Real Board

Build with the hybrid Flash image, then download to the board:

```bash
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/baremetal/snn_head/cpu_ops_selftest clean all
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/baremetal/snn_head/cpu_ops_selftest upload
```

For interactive debugging:

```bash
make SOC=evalsoc CORE=n307fd DOWNLOAD=ilmflashxip \
  PROGRAM=application/baremetal/snn_head/cpu_ops_selftest debug
```

Notes:

- The default board path uses `SoC/evalsoc/Board/nuclei_fpga_eval/openocd_evalsoc.cfg`.
- If the real PAICore board uses a different JTAG/OpenOCD configuration, replace the board config accordingly.
- Generated arrays use the SoC `LARGE_CONST` macro. With
  `DOWNLOAD=ilmflashxip`, the linker places them in Flash/XIP instead of DLM.

## Files

| File | Purpose |
|---|---|
| `Makefile` | Builds the bare-metal ELF and links CPU kernels from `PAIRV/Lib/` |
| `main.c` | Runs all kernel checks and prints PASS/FAIL |
| `gen_test_data.py` | Generates `test_data.h` from the INT8-QAT export state |
| `test_data.h` | Generated C arrays and PyTorch references |
