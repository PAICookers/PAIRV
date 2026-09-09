"""Generate snn_head_golden.c with deployed golden data for board tests.

从 snn_head_int8_quant_export_state.pt 重建 CPU 边界算子，并用当前 PAIBox
lowering 得到的 PAICORE LIF 参数计算三层 spike。不能直接使用 QAT runtime 的
``beta_float``：部署会把 beta 映射到硬件格点，并把严格 ``V > threshold`` 映射为
硬件的 inclusive threshold。对一个固定确定性输入前向，抓取各层边界张量，其表示与 C
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

import argparse
import importlib.util
from pathlib import Path

import snntorch as snn
import torch
from paibox.paiir import torch_to_paiir
from paibox.paiir.ir.op_node import StandaloneActOp
from paicorelib import LeakMultiMode, ThresholdNegMode, ThresholdPosMode
from torch import nn

MODEL_NAME = "snnhead_lif_rdfalse_int8qat_headonly_s1000_q9995_bc_20260701"
MODEL_DIR = Path(__file__).resolve().parents[4].parent / "Applications" / MODEL_NAME
EXPORT_DIR = MODEL_DIR / MODEL_NAME
DEFAULT_PT = EXPORT_DIR / "snn_head_int8_quant_export_state.pt"
NETCODE = EXPORT_DIR / "network_code" / "snntorch_lif_int8_quant.py"
OUT_C = Path(__file__).resolve().parent / "golden" / "snn_head_golden.c"

TIMESTEPS = 8
INPUT_DIM = 768
HIDDEN_DIM = 1536
ACTION_DIM = 7


class DeployedLeakyRuntime:
    """Minimal integer PAICORE LIF reference for the board-test boundaries."""

    def __init__(self, beta: torch.Tensor, threshold: torch.Tensor, size: int):
        """Build a LIF reference from the parameters produced by lowering.

        Args:
            beta: QAT leak parameter consumed by PAIBox lowering.
            threshold: Integer-domain firing threshold from the export state.
            size: Number of neurons in the layer output.
        """
        source = snn.Leaky(
            beta=beta,
            threshold=threshold.to(torch.float32),
            reset_mechanism="subtract",
            reset_delay=False,
            init_hidden=True,
            output=False,
            surrogate_disable=True,
        )
        graph = torch_to_paiir(nn.Sequential(source), torch.zeros(1, size), strict=True)
        acts = [
            node.act
            for node in graph.nodes.values()
            if isinstance(node, StandaloneActOp)
        ]
        if len(acts) != 1:
            raise RuntimeError(f"expected one deployed LIF node, got {len(acts)}")
        act = acts[0]
        self.threshold = torch.as_tensor(act.thres_pos, dtype=torch.int64).reshape(-1)
        self.negative_threshold = torch.as_tensor(
            act.thres_neg, dtype=torch.int64
        ).reshape(-1)
        self.positive_mode = torch.as_tensor(
            act.thres_pos_mode, dtype=torch.int64
        ).reshape(-1)
        self.negative_mode = torch.as_tensor(
            act.thres_neg_mode, dtype=torch.int64
        ).reshape(-1)
        self.mode = torch.as_tensor(act.leak_multi_mode, dtype=torch.int64).reshape(-1)
        self.shift = torch.as_tensor(act.leak_tau, dtype=torch.int64).reshape(-1)
        if any(
            value.numel() not in (1, size)
            for value in (
                self.threshold,
                self.negative_threshold,
                self.positive_mode,
                self.negative_mode,
                self.mode,
                self.shift,
            )
        ):
            raise RuntimeError("deployed LIF parameters do not match output width")
        self.threshold = self.threshold.expand(size)
        self.negative_threshold = self.negative_threshold.expand(size)
        self.positive_mode = self.positive_mode.expand(size)
        self.negative_mode = self.negative_mode.expand(size)
        self.mode = self.mode.expand(size)
        self.shift = self.shift.expand(size)
        self.mem: torch.Tensor | None = None

    def __call__(self, acc: torch.Tensor) -> torch.Tensor:
        """Advance membrane state and return one timestep of spike output.

        Args:
            acc: Integer accumulator values for the current timestep.

        Returns:
            Float32 zero/one spikes after threshold, reset, and leak.
        """
        if self.mem is None:
            self.mem = torch.zeros_like(acc, dtype=torch.int64)
        self.mem += acc.to(torch.int64)
        self.mem = torch.where(
            self.positive_mode == int(ThresholdPosMode.CEILING),
            torch.minimum(self.mem, self.threshold),
            self.mem,
        )
        self.mem = torch.where(
            self.negative_mode == int(ThresholdNegMode.FLOOR),
            torch.maximum(self.mem, self.negative_threshold),
            self.mem,
        )
        spike = self.mem >= self.threshold
        self.mem -= spike.to(torch.int64) * self.threshold

        # PAICORE's post-compare leak uses either direct shift or retention.
        shifted = self.mem >> (-self.shift)
        retained = self.mem - shifted
        self.mem = torch.where(
            self.mode == int(LeakMultiMode.ENABLE), retained, shifted
        )
        return spike.to(torch.float32)


def load_runtime_module(path: Path):
    """Load the exported quantized runtime module from an explicit file.

    Args:
        path: Python source generated with the model export.

    Returns:
        Imported module containing the quantized runtime classes.
    """
    spec = importlib.util.spec_from_file_location("snntorch_lif_int8_quant", str(path))
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def build_model(state: dict, M):
    """Rebuild `QuantizedMLPResNet` directly from frozen export tensors.

    Args:
        state: Exported QAT state dictionary.
        M: Dynamically loaded quantized runtime module.

    Returns:
        Reconstructed quantized SNN Head model.
    """
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
    """Capture layer boundaries using deployed PAICore LIF semantics.

    Args:
        model: Reconstructed quantized SNN Head model.
        x: Input tensor with shape `[1, 8, 768]`.

    Returns:
        The fc1, block0, block1, fc2, and fc3 boundary tensors, each laid out
        as `[timestep, feature]` for direct comparison with the C workspace.
    """
    lif_in = DeployedLeakyRuntime(
        model.lif_in.beta, model.lif_in.threshold_int32, HIDDEN_DIM
    )
    block0_lif = DeployedLeakyRuntime(
        model.mlp_resnet_blocks[0].lif.beta,
        model.mlp_resnet_blocks[0].lif.threshold_int32,
        HIDDEN_DIM,
    )
    block1_lif = DeployedLeakyRuntime(
        model.mlp_resnet_blocks[1].lif.beta,
        model.mlp_resnet_blocks[1].lif.threshold_int32,
        HIDDEN_DIM,
    )
    fc1o, b0o, b1o, fc2o, acto = [], [], [], [], []
    with torch.no_grad():
        for t in range(x.shape[1]):
            xs = x[:, t, :]
            o = model.layer_norm1(xs)
            _, acc = model.fc1(o, return_int=True)
            spk = lif_in(acc)
            fc1o.append(spk)
            block0 = model.mlp_resnet_blocks[0]
            _, acc = block0.linear(block0.layer_norm(spk), return_int=True)
            o = block0_lif(acc)
            b0o.append(o)
            block1 = model.mlp_resnet_blocks[1]
            _, acc = block1.linear(block1.layer_norm(o), return_int=True)
            o = block1_lif(acc)
            b1o.append(o)
            o = model.layer_norm2(o)
            _, acc2 = model.fc2(o, return_int=True)
            mem = acc2.float() * model.fc2.output_scale
            fc2o.append(mem)
            acto.append(model.fc3(mem))

    def stk(lst):
        return torch.stack(lst, dim=1)[0].contiguous()  # [T, dim]

    return stk(fc1o), stk(b0o), stk(b1o), stk(fc2o), stk(acto)


def fmt_float(x: float) -> str:
    """Format one value as a float32-round-trip-safe C literal.

    Args:
        x: Value to round to IEEE-754 binary32.

    Returns:
        C literal with enough significant digits and an `f` suffix.
    """
    v = float(torch.tensor(x, dtype=torch.float32).item())
    s = f"{v:.9g}"
    if (
        ("." not in s)
        and ("e" not in s)
        and ("E" not in s)
        and ("inf" not in s)
        and ("nan" not in s)
    ):
        s += ".0"
    return s + "f"


def emit_array(name: str, values: torch.Tensor) -> str:
    """Render one generated Flash-resident float array.

    Args:
        name: C symbol name.
        values: Tensor to flatten in row-major order.

    Returns:
        Complete C array definition.
    """
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
    """Generate all board-test boundary tensors and return a process status."""
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--pt", type=Path, default=DEFAULT_PT)
    ap.add_argument("--netcode", type=Path, default=NETCODE)
    ap.add_argument(
        "--input",
        type=Path,
        default=None,
        help="可选：加载真实输入 [8,768] 或 [1,8,768]（.npy/.pt）替代 randn",
    )
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
        '__attribute__((section(".large_const_data"), aligned(8)))\n\n'
        "const int snn_head_golden_ready = 1;\n\n"
    )

    body = "\n\n".join(
        [
            emit_array("snn_head_golden_input", inp),
            emit_array("snn_head_golden_fc1_out", fc1o),
            emit_array("snn_head_golden_block0_out", b0o),
            emit_array("snn_head_golden_block1_out", b1o),
            emit_array("snn_head_golden_fc2_out", fc2o),
            emit_array("snn_head_golden_fc3_out", acto),
        ]
    )

    args.out.write_text(header + body + "\n")
    spikes = int((fc1o != 0).sum() + (b0o != 0).sum() + (b1o != 0).sum())
    print(f"wrote {args.out} ({src}); spike_count(fc1+b0+b1)={spikes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
