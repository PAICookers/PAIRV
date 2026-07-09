#!/usr/bin/env python3
"""Generate real-size SNN Head baremetal test data from the INT8 QAT export.

This emits test_data.h for snnhead_real_ops_selftest. The generated C data uses
real exported parameters and realistic SNN Head dimensions:
  - LN1: 768 features, real gamma/beta
  - LN2: 1536 features, real gamma/beta
  - quantize: 1536 fp32 values with real fc3 activation_scale
  - fc3: real int8 weight [7, 1536], real int32 bias [7]
  - dequantize/action: real fc3 output_scale [7]
"""
from __future__ import annotations

import argparse
from pathlib import Path

import torch
import torch.nn.functional as F

ROOT = Path(__file__).resolve().parents[4]
DEFAULT_EXPORT_STATE = (
    ROOT
    / "snnhead_lif_rdfalse_int8qat_headonly_s1000_q9995_bc_20260701"
    / "snn_head_int8_quant_export_state.pt"
)
DEFAULT_OUT = Path(__file__).resolve().parent / "test_data.h"


def c_float(value: float) -> str:
    return f"{float(value):.9e}f"


def emit_array(f, c_type: str, name: str, values, per_line: int) -> None:
    vals = list(values)
    f.write(f"static const {c_type} {name}[{len(vals)}] = {{\n")
    for i in range(0, len(vals), per_line):
        chunk = vals[i : i + per_line]
        f.write("    " + ", ".join(str(v) for v in chunk) + ",\n")
    f.write("};\n\n")


def emit_float_array(f, name: str, tensor: torch.Tensor, per_line: int = 4) -> None:
    vals = [c_float(v) for v in tensor.detach().cpu().reshape(-1).tolist()]
    emit_array(f, "float", name, vals, per_line)


def emit_i8_array(f, name: str, tensor: torch.Tensor, per_line: int = 32) -> None:
    vals = [str(int(v)) for v in tensor.detach().cpu().reshape(-1).tolist()]
    emit_array(f, "int8_t", name, vals, per_line)


def emit_i32_array(f, name: str, tensor: torch.Tensor, per_line: int = 8) -> None:
    vals = [str(int(v)) for v in tensor.detach().cpu().reshape(-1).tolist()]
    emit_array(f, "int32_t", name, vals, per_line)


def quantize_symmetric_int8(x: torch.Tensor, scale: torch.Tensor | float) -> torch.Tensor:
    scale_t = torch.as_tensor(scale, dtype=x.dtype).clamp_min(1e-12)
    return torch.round(x / scale_t).clamp(-127, 127).to(torch.int8)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--export-state", type=Path, default=DEFAULT_EXPORT_STATE)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    state = torch.load(args.export_state, map_location="cpu", weights_only=True)

    ln1_w = state["model.layer_norm1.weight_float"].float()
    ln1_b = state["model.layer_norm1.bias_float"].float()
    ln2_w = state["model.layer_norm2.weight_float"].float()
    ln2_b = state["model.layer_norm2.bias_float"].float()
    fc3_w = state["model.fc3.weight_int8"].to(torch.int8)
    fc3_b = state["model.fc3.bias_int32"].to(torch.int32)
    fc3_act_scale = state["model.fc3.activation_scale"].float().reshape(())
    fc3_output_scale = state["model.fc3.output_scale"].float()

    ln1_dim = ln1_w.numel()
    ln2_dim = ln2_w.numel()
    fc3_out, fc3_in = fc3_w.shape
    if ln1_dim != 768 or ln2_dim != 1536 or fc3_in != 1536 or fc3_out != 7:
        raise RuntimeError(
            f"unexpected dims: ln1={ln1_dim} ln2={ln2_dim} fc3={tuple(fc3_w.shape)}"
        )

    i1 = torch.arange(ln1_dim, dtype=torch.float32)
    ln1_x = torch.linspace(-3.5, 3.5, ln1_dim, dtype=torch.float32) + 0.125 * torch.sin(i1 * 0.017)
    ln1_y = F.layer_norm(ln1_x, (ln1_dim,), ln1_w, ln1_b, eps=1e-5).float()

    i2 = torch.arange(ln2_dim, dtype=torch.float32)
    ln2_x = (((i2.to(torch.int64) * 37 + 11) % 101) < 23).to(torch.float32)
    ln2_y = F.layer_norm(ln2_x, (ln2_dim,), ln2_w, ln2_b, eps=1e-5).float()

    quant_x = ln2_y.contiguous()
    quant_ref = quantize_symmetric_int8(quant_x, fc3_act_scale)

    fc3_x = quant_ref.contiguous()
    fc3_acc = (
        fc3_x.to(torch.int32).reshape(1, fc3_in) @ fc3_w.to(torch.int32).t()
    ).reshape(fc3_out) + fc3_b
    action_ref = fc3_acc.to(torch.float32) * fc3_output_scale

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as f:
        f.write("#ifndef SNNHEAD_REAL_OPS_TEST_DATA_H\n")
        f.write("#define SNNHEAD_REAL_OPS_TEST_DATA_H\n\n")
        f.write("#include <stddef.h>\n#include <stdint.h>\n\n")
        f.write("#define SNN_REAL_LN1_DIM 768U\n")
        f.write("#define SNN_REAL_LN2_DIM 1536U\n")
        f.write("#define SNN_REAL_FC3_IN 1536U\n")
        f.write("#define SNN_REAL_FC3_OUT 7U\n")
        f.write(f"#define SNN_REAL_FC3_ACTIVATION_SCALE {c_float(float(fc3_act_scale.item()))}\n\n")

        emit_float_array(f, "SNN_LN1_X", ln1_x)
        emit_float_array(f, "SNN_LN1_WEIGHT", ln1_w)
        emit_float_array(f, "SNN_LN1_BIAS", ln1_b)
        emit_float_array(f, "SNN_LN1_Y_REF", ln1_y)
        emit_float_array(f, "SNN_LN2_X", ln2_x)
        emit_float_array(f, "SNN_LN2_WEIGHT", ln2_w)
        emit_float_array(f, "SNN_LN2_BIAS", ln2_b)
        emit_float_array(f, "SNN_LN2_Y_REF", ln2_y)
        emit_float_array(f, "SNN_QUANT_X", quant_x)
        emit_i8_array(f, "SNN_QUANT_REF", quant_ref)
        emit_i8_array(f, "SNN_FC3_X", fc3_x)
        emit_i8_array(f, "SNN_FC3_WEIGHT", fc3_w)
        emit_i32_array(f, "SNN_FC3_BIAS", fc3_b)
        emit_i32_array(f, "SNN_FC3_ACC_REF", fc3_acc)
        emit_float_array(f, "SNN_FC3_OUTPUT_SCALE", fc3_output_scale)
        emit_float_array(f, "SNN_ACTION_REF", action_ref)

        f.write("#endif\n")

    print(f"wrote {args.out}")
    print(f"dims: LN1={ln1_dim}, LN2={ln2_dim}, fc3={fc3_in}->{fc3_out}")
    print(f"fc3_activation_scale={float(fc3_act_scale.item()):.9g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
