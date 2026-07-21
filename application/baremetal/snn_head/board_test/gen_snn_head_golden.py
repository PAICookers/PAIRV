#!/usr/bin/env python3
"""Generate snn_head_golden.c: 逐层上板测试的 golden 中间结果。

从 snn_head_int8_quant_export_state.pt 里冻结的量化产物【直接】重建 INT8 golden
runtime（无需浮点权重），对一个固定确定性输入前向，抓取每个层边界张量，其表示与 C
tensor_workspace 完全一致：

  snn_head_golden_input      x             float32[8][768]
  snn_head_golden_fc1_out    lif_in  spike float32[8][1536] (0/1)
  snn_head_golden_block0_out block0  spike float32[8][1536] (0/1)
  snn_head_golden_block1_out block1  spike float32[8][1536] (0/1)
  snn_head_golden_fc2_out    li_out  mem   float32[8][1536] (= mem_int32 * fc2.output_scale)
  snn_head_golden_fc3_out    action        float32[8][7]    (= fc3 acc_int32 * fc3.output_scale)

用法：
  python gen_snn_head_golden.py [--seed N] [--pt PATH] [--input FILE.npy/.pt]

导出包刷新后重跑本脚本即可。生成的 .c 会整体覆盖占位版本并置 ready=1。
"""
from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path

import torch
import torch.nn as nn

DEFAULT_PKG_DIR = Path(
    "/mnt/work/linjiamu/VLA/"
    "snnhead_lif_rdfalse_int8qat_headonly_s1000_q9995_bc_20260701"
)
DEFAULT_PT = DEFAULT_PKG_DIR / "snn_head_int8_quant_export_state.pt"
NETCODE = DEFAULT_PKG_DIR / "network_code" / "snntorch_lif_int8_quant.py"
OUT_C = Path(__file__).resolve().parent / "snn_head_golden.c"

TIMESTEPS = 8
INPUT_DIM = 768
HIDDEN_DIM = 1536
ACTION_DIM = 7


def load_runtime_module(path: Path):
    spec = importlib.util.spec_from_file_location("snntorch_lif_int8_quant", str(path))
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def build_model(state: dict, M):
    """直接用量化产物重建 QuantizedMLPResNet（对齐 build_quantized_head_from_state）。"""
    QLR = M.QuantizedLinearRuntime
    QLK = M.QuantizedLeakyRuntime

    def lin(prefix: str):
        return QLR(
            state[f"{prefix}.weight_int8"],
            state[f"{prefix}.weight_scale"],
            state[f"{prefix}.activation_scale"],
            state[f"{prefix}.bias_int32"],
        )

    def ln(prefix: str, dim: int) -> nn.LayerNorm:
        layer = nn.LayerNorm(dim)
        layer.weight.data.copy_(state[f"{prefix}.weight_float"].float())
        layer.bias.data.copy_(state[f"{prefix}.bias_float"].float())
        return layer

    fc1 = lin("model.fc1")
    lif_in = QLK(
        state["model.lif_in.beta_float"],
        state["model.lif_in.threshold_float"],
        fc1.output_scale,
        "subtract",
    )

    blocks = []
    for i in range(2):
        linear = lin(f"model.mlp_resnet_blocks.{i}.ffn.1")
        lif = QLK(
            state[f"model.mlp_resnet_blocks.{i}.ffn.2.beta_float"],
            state[f"model.mlp_resnet_blocks.{i}.ffn.2.threshold_float"],
            linear.output_scale,
            "subtract",
        )
        blocks.append(
            M.QuantizedMLPBlock(
                ln(f"model.mlp_resnet_blocks.{i}.ffn.0", HIDDEN_DIM), linear, lif
            )
        )

    fc2 = lin("model.fc2")
    li_out = QLK(
        state["model.li_out.beta_float"],
        state["model.li_out.threshold_float"],
        fc2.output_scale,
        "none",
    )
    fc3 = lin("model.fc3")

    return M.QuantizedMLPResNet(
        layer_norm1=ln("model.layer_norm1", INPUT_DIM),
        fc1=fc1,
        lif_in=lif_in,
        blocks=blocks,
        layer_norm2=ln("model.layer_norm2", HIDDEN_DIM),
        fc2=fc2,
        li_out=li_out,
        fc3=fc3,
    )


def run_capture(model, x: torch.Tensor):
    """复刻 QuantizedMLPResNet.forward，逐 timestep 抓每层边界张量（batch=1）。"""
    model.reset_state()
    fc1o, b0o, b1o, fc2o, acto = [], [], [], [], []
    with torch.no_grad():
        for t in range(x.shape[1]):
            xs = x[:, t, :]
            o = model.layer_norm1(xs)
            _, acc = model.fc1(o, return_int=True)
            spk = model.lif_in(acc)
            fc1o.append(spk)
            o = model.mlp_resnet_blocks[0](spk)
            b0o.append(o)
            o = model.mlp_resnet_blocks[1](o)
            b1o.append(o)
            o = model.layer_norm2(o)
            _, acc2 = model.fc2(o, return_int=True)
            model.li_out(acc2)
            mem = model.li_out.mem_float()
            fc2o.append(mem)
            acto.append(model.fc3(mem))

    def stk(lst):
        return torch.stack(lst, dim=1)[0].contiguous()  # [T, dim]

    return stk(fc1o), stk(b0o), stk(b1o), stk(fc2o), stk(acto)


def fmt_float(x: float) -> str:
    v = float(torch.tensor(x, dtype=torch.float32).item())
    s = "%.9g" % v
    if ("." not in s) and ("e" not in s) and ("E" not in s) and ("inf" not in s) \
            and ("nan" not in s):
        s += ".0"
    return s + "f"


def emit_array(name: str, values: torch.Tensor) -> str:
    flat = values.reshape(-1).tolist()
    lines = [f"SNN_HEAD_GOLDEN_DATA const float {name}[{len(flat)}] = {{"]
    row = []
    for i, v in enumerate(flat):
        row.append(fmt_float(v))
        if (len(row) == 8) or (i == len(flat) - 1):
            lines.append("    " + ", ".join(row) + ",")
            row = []
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--pt", type=Path, default=DEFAULT_PT)
    ap.add_argument("--netcode", type=Path, default=NETCODE)
    ap.add_argument("--input", type=Path, default=None,
                    help="可选：加载真实输入 [8,768] 或 [1,8,768]（.npy/.pt）替代 randn")
    ap.add_argument("--out", type=Path, default=OUT_C)
    args = ap.parse_args()

    M = load_runtime_module(args.netcode)
    state = torch.load(args.pt, map_location="cpu", weights_only=False)
    model = build_model(state, M).eval()

    if args.input is not None:
        if args.input.suffix == ".npy":
            import numpy as np
            x = torch.from_numpy(np.load(args.input)).float()
        else:
            x = torch.load(args.input, map_location="cpu", weights_only=False).float()
        if x.ndim == 2:
            x = x.unsqueeze(0)
        assert tuple(x.shape) == (1, TIMESTEPS, INPUT_DIM), x.shape
        src = f"--input {args.input.name}"
    else:
        g = torch.Generator().manual_seed(args.seed)
        x = torch.randn(1, TIMESTEPS, INPUT_DIM, generator=g)
        src = f"randn seed={args.seed}"

    fc1o, b0o, b1o, fc2o, acto = run_capture(model, x)
    inp = x[0].contiguous()  # [8,768]

    assert tuple(inp.shape) == (TIMESTEPS, INPUT_DIM)
    for name, arr in (("fc1", fc1o), ("b0", b0o), ("b1", b1o), ("fc2", fc2o)):
        assert tuple(arr.shape) == (TIMESTEPS, HIDDEN_DIM), (name, arr.shape)
    assert tuple(acto.shape) == (TIMESTEPS, ACTION_DIM)

    header = (
        "/*\n"
        " * 逐层上板测试 golden 数据（由 gen_snn_head_golden.py 生成，请勿手改）。\n"
        f" * 输入来源: {src}\n"
        f" * 量化产物: {args.pt.name}\n"
        " *\n"
        " * 各数组表示与 C tensor_workspace 完全一致，行主序 [timestep][dim]。\n"
        " * 大数组放入 .large_const_data 段（Flash XIP），不占 DLM/RAM。\n"
        " */\n"
        '#include "snn_head_golden.h"\n\n'
        "#define SNN_HEAD_GOLDEN_DATA "
        "__attribute__((section(\".large_const_data\"), aligned(8)))\n\n"
        "const int snn_head_golden_ready = 1;\n\n"
    )

    body = "\n\n".join([
        emit_array("snn_head_golden_input", inp),
        emit_array("snn_head_golden_fc1_out", fc1o),
        emit_array("snn_head_golden_block0_out", b0o),
        emit_array("snn_head_golden_block1_out", b1o),
        emit_array("snn_head_golden_fc2_out", fc2o),
        emit_array("snn_head_golden_fc3_out", acto),
    ])

    args.out.write_text(header + body + "\n")
    spikes = int((fc1o != 0).sum() + (b0o != 0).sum() + (b1o != 0).sum())
    print(f"wrote {args.out} ({src}); spike_count(fc1+b0+b1)={spikes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
