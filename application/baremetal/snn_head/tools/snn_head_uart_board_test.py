"""Validate repeated production or single diagnostic SNN Head transactions.

Every INPUT contains the production `8 x 768` float32 tensor. Raw protocol,
startup, debug, timing, and summary evidence is retained with `--log-dir`.
"""

import argparse
import json
import math
import random
import struct
import sys
import time
import zlib
from collections.abc import Sequence
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

MAGIC = b"NSNN"
VERSION = 1
HEADER_SIZE = 48
FLOAT32_BYTES = 4
INPUT_SHAPE = (1, 8, 768)
RESULT_SHAPE = (1, 8, 7)
INPUT_FLOAT_COUNT = math.prod(INPUT_SHAPE)
RESULT_FLOAT_COUNT = math.prod(RESULT_SHAPE)
INPUT_PAYLOAD_BYTES = INPUT_FLOAT_COUNT * FLOAT32_BYTES
RESULT_PAYLOAD_BYTES = RESULT_FLOAT_COUNT * FLOAT32_BYTES
DTYPE_FLOAT32_LE = 1

TYPE_INPUT = 1
TYPE_INPUT_ACK = 2
TYPE_RESULT = 3
TYPE_ERROR = 4

STATUS_OK = 0
STATUS_BAD_PAYLOAD = 2
STATUS_INFERENCE_FAILED = 4
HEADER_STRUCT = struct.Struct("<4sBBHIIIIHHHHQII")
TIMING_END = b"SNN_HEAD_TIMING_END\n"
TIMING_LOG_MAX_BYTES = 16 * 1024
DEBUG_PREFIX = b"SNN_HEAD_DEBUG "
DEBUG_LOG_MAX_BYTES = 512


class ProtocolError(Exception):
    """Raised when the board response does not satisfy the NSNN contract."""


class TransportTimeout(Exception):
    """Raised when a serial operation exceeds its transaction deadline."""


@dataclass(frozen=True)
class Header:
    """Decoded fields from the fixed NSNN v1 wire header.

    Parameters:
        version: Protocol version.
        frame_type: INPUT, ACK, RESULT, or ERROR type identifier.
        header_size: Encoded header size in bytes.
        seq: Transaction sequence number.
        length: Payload size in bytes.
        payload_crc: CRC-32/ISO-HDLC of the payload.
        status: Protocol status code.
        d0: Batch dimension.
        d1: Timestep dimension.
        d2: Feature dimension.
        dtype: Wire data-type identifier.
        cycles: Complete inference cycles reported by the target.
        hz: Cycle-counter frequency reported by the target.
        magic: Four-byte NSNN synchronization marker.
    """

    version: int
    frame_type: int
    header_size: int
    seq: int
    length: int
    payload_crc: int
    status: int
    d0: int
    d1: int
    d2: int
    dtype: int
    cycles: int
    hz: int
    magic: bytes = MAGIC


@dataclass(frozen=True)
class TransactionResult:
    """Validated metadata retained from one successful transaction.

    Parameters:
        seq: Completed transaction sequence number.
        cycles: Complete inference cycles from RESULT.
        hz: Cycle-counter frequency from RESULT.
        inference_seconds: Target inference duration derived from cycles.
        elapsed_seconds: Host-observed transaction duration.
        result_payload_crc: Validated RESULT payload CRC.
    """

    seq: int
    cycles: int
    hz: int
    inference_seconds: float
    elapsed_seconds: float
    result_payload_crc: int


def crc32(data: bytes) -> int:
    """Return CRC-32/ISO-HDLC for the NSNN wire bytes."""
    return zlib.crc32(data) & 0xFFFFFFFF


def pack_header(header: Header) -> bytes:
    """Encode one fixed 48-byte NSNN header."""
    raw = HEADER_STRUCT.pack(
        header.magic,
        header.version,
        header.frame_type,
        header.header_size,
        header.seq,
        header.length,
        header.payload_crc,
        header.status,
        header.d0,
        header.d1,
        header.d2,
        header.dtype,
        header.cycles,
        header.hz,
        0,
    )
    return raw[:44] + struct.pack("<I", crc32(raw[:44]))


def unpack_header(raw: bytes) -> Header:
    """Decode and validate structural fields and CRC of one NSNN header."""
    if len(raw) != HEADER_SIZE:
        raise ProtocolError(f"header length {len(raw)}, expected {HEADER_SIZE}")
    values = HEADER_STRUCT.unpack(raw)
    if values[0] != MAGIC:
        raise ProtocolError(f"bad magic {values[0]!r}")
    if values[1] != VERSION or values[3] != HEADER_SIZE:
        raise ProtocolError("unsupported NSNN version or header size")
    if values[14] != crc32(raw[:44]):
        raise ProtocolError("header CRC mismatch")
    return Header(
        magic=values[0],
        version=values[1],
        frame_type=values[2],
        header_size=values[3],
        seq=values[4],
        length=values[5],
        payload_crc=values[6],
        status=values[7],
        d0=values[8],
        d1=values[9],
        d2=values[10],
        dtype=values[11],
        cycles=values[12],
        hz=values[13],
    )


def read_exact(port: Any, size: int, deadline: float) -> bytes:
    """Read exactly `size` bytes before an absolute monotonic deadline.

    Args:
        port: Open pyserial-compatible port whose timeout is restored on exit.
        size: Required number of bytes.
        deadline: Absolute `time.monotonic()` deadline.

    Returns:
        Exactly `size` received bytes.

    Raises:
        TransportTimeout: If the deadline expires before the buffer is full.
    """
    received = bytearray()
    while len(received) < size:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TransportTimeout(f"read timeout after {len(received)}/{size} bytes")
        previous_timeout = port.timeout
        port.timeout = min(0.1, remaining)
        try:
            chunk = port.read(size - len(received))
        finally:
            port.timeout = previous_timeout
        received.extend(chunk)
    return bytes(received)


def write_all(port: Any, data: bytes, deadline: float) -> None:
    """Write all bytes before an absolute monotonic deadline.

    Args:
        port: Open pyserial-compatible port whose timeout is restored on exit.
        data: Bytes to transmit.
        deadline: Absolute `time.monotonic()` deadline.

    Raises:
        TransportTimeout: If all bytes cannot be written before the deadline.
    """
    sent = 0
    while sent < len(data):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TransportTimeout(f"write timeout after {sent}/{len(data)} bytes")
        previous_timeout = port.write_timeout
        port.write_timeout = remaining
        try:
            count = port.write(data[sent:])
        finally:
            port.write_timeout = previous_timeout
        if count is None or count <= 0:
            continue
        sent += count


def payload_from_seed(seed: int) -> bytes:
    """Build finite float32[1][8][768] input from a reproducible seed."""
    generator = random.Random(seed)
    values = (generator.uniform(-1.0, 1.0) for _ in range(INPUT_FLOAT_COUNT))
    return struct.pack(f"<{INPUT_FLOAT_COUNT}f", *values)


def expected_timestep_prefix(payload: bytes) -> bytes:
    """Return input[t][0:7] for every timestep as little-endian bytes."""
    return b"".join(
        payload[
            timestep * INPUT_SHAPE[2] * FLOAT32_BYTES : (
                timestep * INPUT_SHAPE[2] + RESULT_SHAPE[2]
            )
            * FLOAT32_BYTES
        ]
        for timestep in range(INPUT_SHAPE[1])
    )


def input_frame(payload: bytes, seq: int) -> bytes:
    """Build one fixed-shape INPUT frame."""
    if len(payload) != INPUT_PAYLOAD_BYTES:
        raise ValueError(
            f"input payload is {len(payload)}, expected {INPUT_PAYLOAD_BYTES}"
        )
    header = Header(
        version=VERSION,
        frame_type=TYPE_INPUT,
        header_size=HEADER_SIZE,
        seq=seq,
        length=len(payload),
        payload_crc=crc32(payload),
        status=STATUS_OK,
        d0=INPUT_SHAPE[0],
        d1=INPUT_SHAPE[1],
        d2=INPUT_SHAPE[2],
        dtype=DTYPE_FLOAT32_LE,
        cycles=0,
        hz=0,
    )
    return pack_header(header) + payload


def wait_for_banner(port: Any, timeout: float) -> bytes:
    """Capture startup text until the application enters its service loop.

    Args:
        port: Open serial port positioned before firmware startup.
        timeout: Maximum host wait in seconds.

    Returns:
        Captured Banner bytes including `SNN_HEAD_UART_READY`.

    Raises:
        TransportTimeout: If the ready marker is not observed in time.
    """
    deadline = time.monotonic() + timeout
    captured = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(256)
        if chunk:
            captured.extend(chunk)
            if b"SNN_HEAD_UART_READY\r\n" in captured:
                return bytes(captured)
    raise TransportTimeout("timed out waiting for SNN Head UART ready marker")


def read_timing_log(port: Any, timeout: float) -> bytes:
    """Capture the post-RESULT report from a timing-enabled image.

    Args:
        port: Open serial port positioned immediately after RESULT.
        timeout: Maximum host wait in seconds.

    Returns:
        Complete ASCII timing block.

    Raises:
        TransportTimeout: If the end marker is not observed in time.
    """
    deadline = time.monotonic() + timeout
    captured = bytearray()
    while time.monotonic() < deadline:
        previous_timeout = port.timeout
        port.timeout = min(0.1, deadline - time.monotonic())
        try:
            chunk = port.read(256)
        finally:
            port.timeout = previous_timeout
        if chunk:
            captured.extend(chunk)
            if len(captured) > TIMING_LOG_MAX_BYTES:
                raise ProtocolError("timing report exceeds maximum length")
            if TIMING_END in captured:
                return bytes(captured)
    raise TransportTimeout("timed out waiting for SNN Head timing report")


def read_debug_log(port: Any, timeout: float) -> bytes:
    """Capture one bounded post-response debug summary line."""
    deadline = time.monotonic() + timeout
    captured = bytearray()
    while time.monotonic() < deadline:
        previous_timeout = port.timeout
        port.timeout = min(0.1, deadline - time.monotonic())
        try:
            chunk = port.read(1)
        finally:
            port.timeout = previous_timeout
        if not chunk:
            continue
        captured.extend(chunk)
        if len(captured) > DEBUG_LOG_MAX_BYTES:
            raise ProtocolError("debug summary exceeds maximum length")
        if chunk == b"\n":
            data = bytes(captured)
            if not data.startswith(DEBUG_PREFIX):
                raise ProtocolError("invalid debug summary prefix")
            return data
    raise TransportTimeout("timed out waiting for SNN Head debug summary")


def validate_timing_log(data: bytes) -> None:
    """Require the complete timing report and its five-layer step records.

    Args:
        data: ASCII timing block emitted after RESULT.

    Raises:
        ProtocolError: If required fields, layers, or timestep records are
            missing or internally inconsistent.
    """
    try:
        lines = data.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise ProtocolError("timing report is not ASCII") from error
    if (
        not lines
        or not lines[0].startswith("SNN_HEAD_TIMING_BEGIN ")
        or not data.endswith(TIMING_END)
        or lines[-1] != "SNN_HEAD_TIMING_END"
    ):
        raise ProtocolError("incomplete timing report")
    expected_layers = {"fc1_lif", "block0", "block1", "fc2", "fc3"}
    observed_layers: set[str] = set()
    required: dict[str, tuple[str, ...]] = {
        "SNN_HEAD_TIMING_BEGIN": ("hz", "chain_cycles"),
        "SNN_HEAD_TIMING uart_rx_payload": ("cycles",),
        "SNN_HEAD_TIMING uart_decode_float": ("cycles",),
        "SNN_HEAD_TIMING uart_result_encode_tx": ("cycles",),
    }
    layer_steps: dict[str, list[int]] = {name: [] for name in expected_layers}
    layer_step_cycles: dict[str, list[int]] = {name: [] for name in expected_layers}
    layer_sync_totals: dict[str, int] = {}
    for line in lines[:-1]:
        fields = dict(token.split("=", 1) for token in line.split() if "=" in token)
        keys = (
            required["SNN_HEAD_TIMING_BEGIN"]
            if line.startswith("SNN_HEAD_TIMING_BEGIN ")
            else required.get(" ".join(line.split()[:2]))
        )
        if line.startswith("SNN_HEAD_TIMING layer="):
            layer = fields.get("layer", "")
            observed_layers.add(layer)
            keys = (
                "total_cycles",
                "cpu_preprocess_cycles",
                "artifact_validate_cycles",
                "deploy_cycles",
                "sample_cycles",
                "config_submit_cycles",
                "input_encode_cycles",
                "input_submit_cycles",
                "init_round_trip_cycles",
                "sync_round_trip_cycles",
                "release_cycles",
                "cpu_postprocess_cycles",
                "residual_cycles",
                "sync_wait_cycles",
                "rx_irq_service_cycles",
                "config_frames",
                "input_frames",
                "output_work_frames",
                "complete_frames",
                "rx_irq_count",
            )
        if line.startswith("SNN_HEAD_TIMING step layer="):
            keys = ("timestep", "sync_round_trip_cycles")
            layer = fields.get("layer", "")
            if layer in layer_steps:
                layer_steps[layer].append(int(fields.get("timestep", "-1")))
                layer_step_cycles[layer].append(
                    int(fields.get("sync_round_trip_cycles", "-1"))
                )
        if keys is not None and any(not fields.get(key, "").isdigit() for key in keys):
            raise ProtocolError(f"non-decimal timing field: {line}")
        if line.startswith("SNN_HEAD_TIMING layer="):
            layer_sync_totals[layer] = int(fields["sync_round_trip_cycles"])
    if observed_layers != expected_layers:
        raise ProtocolError(f"unexpected timing layers: {sorted(observed_layers)}")
    expected_steps = list(range(8))
    if any(sorted(steps) != expected_steps for steps in layer_steps.values()):
        raise ProtocolError("missing or repeated per-timestep SYNC timings")
    if any(
        layer_sync_totals.get(layer) != sum(layer_step_cycles[layer])
        for layer in expected_layers
    ):
        raise ProtocolError("per-layer SYNC aggregate does not match step timings")


def timing_fields(line: str) -> dict[str, str]:
    """Extract the key=value fields from one validated timing line."""
    return dict(token.split("=", 1) for token in line.split() if "=" in token)


def timing_duration_ms(cycles: float, hz: int) -> str:
    """Format a raw MCU cycle count using the frequency returned by RESULT."""
    return f"{cycles * 1_000.0 / hz:.6f}"


def timing_row(name: str, cycles: int, hz: int) -> str:
    """Render one raw-cycle and host-converted duration table row."""
    return f"| {name} | {cycles} | {timing_duration_ms(cycles, hz)} |\n"


def timing_sync_average_row(
    layer: str, total_cycles: int, timestep_count: int, hz: int
) -> str:
    """Render one SYNC-to-COMPLETE aggregate and per-timestep average row."""
    average_cycles = total_cycles / timestep_count
    return (
        f"| {layer} | {total_cycles} | {average_cycles:.3f} | "
        f"{timing_duration_ms(average_cycles, hz)} |\n"
    )


def timing_report(data: bytes, result: TransactionResult) -> bytes:
    """Render the MCU cycle-only profile as a Chinese Markdown report.

    Args:
        data: Validatable ASCII timing block from the target.
        result: RESULT metadata for cross-checking frequency and chain cycles.

    Returns:
        UTF-8 Markdown report with complete-chain, layer, and runner timings.

    Raises:
        ProtocolError: If timing data is incomplete or inconsistent.
    """
    validate_timing_log(data)
    lines = data.decode("ascii").splitlines()
    begin = timing_fields(lines[0])
    profile_hz = int(begin["hz"])
    profile_cycles = int(begin["chain_cycles"])
    if (profile_hz != result.hz) or (profile_cycles != result.cycles):
        raise ProtocolError("RESULT timing metadata does not match timing profile")
    layer_lines = [line for line in lines if line.startswith("SNN_HEAD_TIMING layer=")]
    layer_total_cycles = sum(
        int(timing_fields(line)["total_cycles"]) for line in layer_lines
    )
    if layer_total_cycles > result.cycles:
        raise ProtocolError("layer totals exceed complete inference timing")
    timestep_count = INPUT_SHAPE[1]

    report = [
        "# SNN Head 阶段耗时报告\n\n",
        "## 测量说明\n\n",
        "- MCU 仅输出原始 `mcycle` 计数。\n",
        "- 主机换算公式：`耗时(ms) = cycles * 1000 / hz`。\n",
        f"- RESULT 返回的时钟频率：`{result.hz}` Hz。\n",
        (
            "- 本次事务：`float32[1][8][768]` 输入，完整五层链路，"
            "`float32[1][8][7]` 输出。\n\n"
        ),
        "## 完整推理\n\n",
        "| 指标 | Cycles | 主机换算耗时 (ms) |\n",
        "| --- | ---: | ---: |\n",
        timing_row("snn_head_run_chunk 完整链路", result.cycles, result.hz),
        timing_row("五层总计", layer_total_cycles, result.hz),
        timing_row(
            "链路编排与 profile 开销", result.cycles - layer_total_cycles, result.hz
        ),
        "\n## UART 传输\n\n",
        "| 指标 | Cycles | 主机换算耗时 (ms) |\n",
        "| --- | ---: | ---: |\n",
    ]
    for line in lines:
        fields = timing_fields(line)
        if line.startswith("SNN_HEAD_TIMING uart_rx_payload"):
            report.append(timing_row("输入负载接收", int(fields["cycles"]), result.hz))
        elif line.startswith("SNN_HEAD_TIMING uart_decode_float"):
            report.append(
                timing_row("输入 float32 解码", int(fields["cycles"]), result.hz)
            )
        elif line.startswith("SNN_HEAD_TIMING uart_result_encode_tx"):
            report.append(
                timing_row("输出 float32 编码与发送", int(fields["cycles"]), result.hz)
            )

    report.extend(
        [
            "\n## 各层总耗时\n\n",
            (
                "各层总计是 `snn_head_run_chunk()` 中顺序执行的部分。完整推理表另列了"
                "极小的外层链路编排与 profile 开销。\n\n"
            ),
            "| 层 | Cycles | 主机换算耗时 (ms) |\n",
            "| --- | ---: | ---: |\n",
        ]
    )
    for line in layer_lines:
        fields = timing_fields(line)
        report.append(
            timing_row(fields["layer"], int(fields["total_cycles"]), result.hz)
        )
    report.append(timing_row("五层总计", layer_total_cycles, result.hz))

    report.extend(
        [
            "\n## 各层模型耗时\n\n",
            (
                "模型耗时 = 层总计 - 编译产物校验 - PAICore 部署/配置"
                " - CPU 前处理 - CPU 后处理。deploy_cycles 已包含模型配置、"
                "配置帧发送和 runner 初始化；结果保留 sample、运行器释放和未归类"
                "开销，不是纯 PAICore 核内耗时。\n\n"
            ),
            "| 层 | Cycles | 主机换算耗时 (ms) |\n",
            "| --- | ---: | ---: |\n",
        ]
    )
    model_total_cycles = 0
    for line in layer_lines:
        fields = timing_fields(line)
        total_cycles = int(fields["total_cycles"])
        artifact_validate_cycles = int(fields["artifact_validate_cycles"])
        deploy_cycles = int(fields["deploy_cycles"])
        cpu_preprocess_cycles = int(fields["cpu_preprocess_cycles"])
        cpu_postprocess_cycles = int(fields["cpu_postprocess_cycles"])
        preparation_cycles = (
            artifact_validate_cycles
            + deploy_cycles
            + cpu_preprocess_cycles
            + cpu_postprocess_cycles
        )
        if preparation_cycles > total_cycles:
            raise ProtocolError("excluded timing exceeds layer total")
        model_cycles = total_cycles - preparation_cycles
        model_total_cycles += model_cycles
        report.append(
            f"| {fields['layer']} | {model_cycles} | "
            f"{timing_duration_ms(model_cycles, result.hz)} |\n"
        )
    report.append(timing_row("五层总计", model_total_cycles, result.hz))

    report.extend(
        [
            "\n## 各层阶段\n\n",
            (
                "每层以下阶段可相加：`总计 = CPU 前处理 + Flash 编译产物结构与接口校验 + PAICore 部署 + "
                "PAICore sample 执行 + 运行器释放 + CPU 后处理 + 未归类开销`。\n\n"
            ),
        ]
    )
    phase_fields = (
        ("cpu_preprocess_cycles", "CPU 前处理"),
        ("artifact_validate_cycles", "Flash 编译产物结构与接口校验"),
        ("deploy_cycles", "PAICore 部署"),
        ("sample_cycles", "PAICore sample 执行"),
        ("release_cycles", "运行器释放"),
        ("cpu_postprocess_cycles", "CPU 后处理"),
        ("residual_cycles", "未归类开销"),
    )
    runner_fields = (
        ("config_submit_cycles", "配置帧查找与提交（PAICore 部署内）"),
        ("input_encode_cycles", "输入帧编码（PAICore sample 执行内）"),
        ("input_submit_cycles", "输入帧提交（PAICore sample 执行内）"),
        ("init_round_trip_cycles", "INIT 往返（PAICore sample 执行内）"),
        ("sync_round_trip_cycles", "发送 SYNC 至收到 COMPLETE（8 步合计）"),
        ("sync_wait_cycles", "SYNC 至 COMPLETE 的等待（上述期间内）"),
        ("rx_irq_service_cycles", "RX IRQ 服务（与 SYNC 至 COMPLETE 重叠）"),
    )
    for line in layer_lines:
        fields = timing_fields(line)
        layer = fields["layer"]
        report.extend(
            [
                f"### {layer}\n\n",
                "| 阶段 | Cycles | 主机换算耗时 (ms) |\n",
                "| --- | ---: | ---: |\n",
            ]
        )
        for field, name in phase_fields:
            report.append(timing_row(name, int(fields[field]), result.hz))
        report.extend(
            [
                (
                    "\n运行器明细是嵌套观测，不能与 `sample` 或彼此相加。"
                    "`sync_round_trip` 从发送 SYNC 到接收 COMPLETE，涵盖 PAICore、NoC、"
                    "IRQ 与输出处理，不是纯 PAICore 核内计数器。\n\n"
                ),
                "| 嵌套运行器指标 | Cycles | 主机换算耗时 (ms) |\n",
                "| --- | ---: | ---: |\n",
            ]
        )
        for field, name in runner_fields:
            report.append(timing_row(name, int(fields[field]), result.hz))
        report.append(
            "\n"
            f"帧计数：配置 `{fields['config_frames']}`，输入 "
            f"`{fields['input_frames']}`，输出 WORK "
            f"`{fields['output_work_frames']}`，COMPLETE "
            f"`{fields['complete_frames']}`，RX IRQ "
            f"`{fields['rx_irq_count']}`。\n\n"
        )

    report.extend(
        [
            f"## 每层 {timestep_count} 个时间步的 SYNC 至 COMPLETE 平均耗时\n\n",
            "| 层 | 8 步合计 Cycles | 每步平均 Cycles | 每步平均耗时 (ms) |\n",
            "| --- | ---: | ---: | ---: |\n",
        ]
    )
    for line in layer_lines:
        fields = timing_fields(line)
        report.append(
            timing_sync_average_row(
                fields["layer"],
                int(fields["sync_round_trip_cycles"]),
                timestep_count,
                result.hz,
            )
        )

    return "".join(report).encode("utf-8")


def check_shape(header: Header, expected: tuple[int, int, int]) -> None:
    """Validate a tensor header's shape and little-endian float32 dtype."""
    if (header.d0, header.d1, header.d2, header.dtype) != (
        *expected,
        DTYPE_FLOAT32_LE,
    ):
        raise ProtocolError(
            "unexpected shape/dtype "
            f"{header.d0}x{header.d1}x{header.d2} dtype={header.dtype}"
        )


def transaction(
    port: Any,
    payload: bytes,
    seq: int,
    timeout: float,
    rx: bytearray,
    expected_result_payload: bytes | None = None,
    input_prefix: bytes = b"",
) -> tuple[TransactionResult, bytes, bytes]:
    """Send INPUT and return validated RESULT metadata plus raw bytes.

    Args:
        port: Open pyserial-compatible port.
        payload: Complete little-endian `[1,8,768]` float payload.
        seq: Transaction sequence number.
        timeout: Absolute transaction budget in seconds.
        rx: Accumulator for every received byte.
        expected_result_payload: Optional exact RESULT payload for the
            standalone float-UART test.
        input_prefix: Optional bytes sent before NSNN to test resynchronization.

    Returns:
        Validated result metadata, bytes transmitted for this transaction, and
        the complete accumulated receive stream.

    Raises:
        ProtocolError: If ACK, RESULT, shape, CRC, or payload checks fail.
        TransportTimeout: If serial I/O exceeds the transaction deadline.
    """
    tx = input_prefix + input_frame(payload, seq)
    started = time.monotonic()
    deadline = started + timeout
    write_all(port, tx, deadline)

    ack_raw = read_exact(port, HEADER_SIZE, deadline)
    rx.extend(ack_raw)
    ack = unpack_header(ack_raw)
    if ack.frame_type == TYPE_ERROR:
        raise ProtocolError(f"board ERROR status={ack.status} seq={ack.seq}")
    if (
        ack.frame_type != TYPE_INPUT_ACK
        or ack.seq != seq
        or ack.length != 0
        or ack.payload_crc != 0
        or ack.status != STATUS_OK
    ):
        raise ProtocolError("invalid INPUT_ACK")
    check_shape(ack, INPUT_SHAPE)

    result_raw = read_exact(port, HEADER_SIZE, deadline)
    rx.extend(result_raw)
    result = unpack_header(result_raw)
    if result.frame_type == TYPE_ERROR:
        raise ProtocolError(f"board ERROR status={result.status} seq={result.seq}")
    if (
        result.frame_type != TYPE_RESULT
        or result.seq != seq
        or result.status != STATUS_OK
        or result.length != RESULT_PAYLOAD_BYTES
    ):
        raise ProtocolError("invalid RESULT header")
    check_shape(result, RESULT_SHAPE)
    if result.hz == 0:
        raise ProtocolError("RESULT must contain a non-zero clock frequency")

    result_payload = read_exact(port, RESULT_PAYLOAD_BYTES, deadline)
    rx.extend(result_payload)
    if crc32(result_payload) != result.payload_crc:
        raise ProtocolError("RESULT payload CRC mismatch")
    if (
        expected_result_payload is not None
        and result_payload != expected_result_payload
    ):
        raise ProtocolError("RESULT does not match the expected input prefix")
    output = struct.unpack(f"<{RESULT_FLOAT_COUNT}f", result_payload)
    if not all(math.isfinite(value) for value in output):
        raise ProtocolError("RESULT contains a non-finite float")

    return (
        TransactionResult(
            seq=seq,
            cycles=result.cycles,
            hz=result.hz,
            inference_seconds=result.cycles / result.hz,
            elapsed_seconds=time.monotonic() - started,
            result_payload_crc=result.payload_crc,
        ),
        tx,
        bytes(rx),
    )


def check_nonfinite_input_rejected(
    port: Any, payload: bytes, seq: int, timeout: float, rx: bytearray
) -> bytes:
    """Send one valid-CRC NaN input and require BAD_PAYLOAD before ACK.

    Args:
        port: Open serial port connected to the standalone target.
        payload: Finite payload used as the mutation base.
        seq: Transaction sequence number.
        timeout: Transaction timeout in seconds.
        rx: Receive accumulator updated in place.

    Returns:
        Complete transmitted INPUT frame.

    Raises:
        ProtocolError: If the target accepts the non-finite input.
        TransportTimeout: If the ERROR header is not received in time.
    """
    nonfinite_payload = struct.pack("<I", 0x7FC00000) + payload[FLOAT32_BYTES:]
    tx = input_frame(nonfinite_payload, seq)
    deadline = time.monotonic() + timeout
    write_all(port, tx, deadline)
    raw = read_exact(port, HEADER_SIZE, deadline)
    rx.extend(raw)
    error = unpack_header(raw)
    if (
        error.frame_type != TYPE_ERROR
        or error.seq != seq
        or error.status != STATUS_BAD_PAYLOAD
        or error.length != 0
    ):
        raise ProtocolError("non-finite INPUT was not rejected as BAD_PAYLOAD")
    return tx


def check_nonfinite_output_rejected(
    port: Any, payload: bytes, seq: int, timeout: float, rx: bytearray
) -> bytes:
    """Trigger the float-UART test sentinel and require inference ERROR.

    Args:
        port: Open serial port connected to `float_uart_test`.
        payload: Finite payload used as the mutation base.
        seq: Transaction sequence number.
        timeout: Transaction timeout in seconds.
        rx: Receive accumulator updated in place.

    Returns:
        Complete transmitted INPUT frame.

    Raises:
        ProtocolError: If ACK or inference ERROR semantics are incorrect.
        TransportTimeout: If either expected header is not received in time.
    """
    sentinel_payload = struct.pack("<f", 12345.0) + payload[FLOAT32_BYTES:]
    tx = input_frame(sentinel_payload, seq)
    deadline = time.monotonic() + timeout
    write_all(port, tx, deadline)

    ack_raw = read_exact(port, HEADER_SIZE, deadline)
    rx.extend(ack_raw)
    ack = unpack_header(ack_raw)
    if (
        ack.frame_type != TYPE_INPUT_ACK
        or ack.seq != seq
        or ack.status != STATUS_OK
        or ack.length != 0
    ):
        raise ProtocolError("finite sentinel INPUT did not receive INPUT_ACK")

    error_raw = read_exact(port, HEADER_SIZE, deadline)
    rx.extend(error_raw)
    error = unpack_header(error_raw)
    if (
        error.frame_type != TYPE_ERROR
        or error.seq != seq
        or error.status != STATUS_INFERENCE_FAILED
        or error.length != 0
    ):
        raise ProtocolError("non-finite output was not rejected as inference failure")
    return tx


def write_capture(
    log_dir: Path | None, files: dict[str, bytes], summary: dict[str, Any]
) -> None:
    """Persist raw evidence without changing the transaction result.

    Args:
        log_dir: Destination directory, or `None` to disable capture.
        files: Mapping of evidence filenames to raw bytes.
        summary: JSON-serializable transaction summary.
    """
    if log_dir is None:
        return
    log_dir.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (log_dir / name).write_bytes(data)
    (log_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    """Parse command-line options.

    Args:
        argv: Optional argument sequence; uses `sys.argv` when omitted.

    Returns:
        Parsed board-test configuration.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="stable serial device path")
    parser.add_argument("--baud", type=int, default=3_000_000)
    parser.add_argument("--seed", type=int, default=20_260_906)
    parser.add_argument("--seq", type=int, default=1)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--startup-delay", type=float, default=1.0)
    parser.add_argument("--await-banner", action="store_true")
    parser.add_argument("--banner-timeout", type=float, default=60.0)
    parser.add_argument("--capture-timing-log", action="store_true")
    parser.add_argument("--capture-debug-log", action="store_true")
    parser.add_argument(
        "--float-uart-test",
        action="store_true",
        help="run exact float_uart_test mapping, resync, and finite guards",
    )
    parser.add_argument(
        "--report-timeout",
        "--timing-log-timeout",
        dest="report_timeout",
        type=float,
        default=5.0,
    )
    parser.add_argument("--log-dir", type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    """Run board transactions, validate responses, and save evidence.

    Args:
        argv: Optional command-line arguments for tests and embedding.

    Returns:
        Zero on success, one on transaction failure, or two on invalid setup.
    """
    args = parse_args(argv)
    if (
        args.runs <= 0
        or args.timeout <= 0
        or args.startup_delay < 0
        or args.banner_timeout <= 0
        or args.report_timeout <= 0
    ):
        print("ERROR: runs and timeout values must be positive", file=sys.stderr)
        return 2
    if (args.capture_timing_log or args.capture_debug_log) and args.runs != 1:
        print("ERROR: diagnostic capture requires --runs 1", file=sys.stderr)
        return 2
    if args.seq < 0 or args.seq + args.runs + 1 > 0xFFFFFFFF:
        print("ERROR: sequence range exceeds uint32", file=sys.stderr)
        return 2
    if args.capture_timing_log and args.capture_debug_log:
        print("ERROR: debug and timing capture are mutually exclusive", file=sys.stderr)
        return 2
    if args.float_uart_test and (args.capture_timing_log or args.capture_debug_log):
        print("ERROR: --float-uart-test does not emit diagnostics", file=sys.stderr)
        return 2
    try:
        import serial
    except ImportError:
        print("ERROR: pyserial is required for board access", file=sys.stderr)
        return 2

    payloads = [payload_from_seed(args.seed + index) for index in range(args.runs)]
    payload = payloads[0]
    summary: dict[str, Any] = {
        "status": "FAIL",
        "port": args.port,
        "baud": args.baud,
        "seed": args.seed,
        "seq": args.seq,
        "runs": args.runs,
        "input_payload_bytes": len(payload),
        "input_payload_crc": crc32(payload),
    }
    banner = b""
    timing_log = b""
    debug_log = b""
    timing_markdown = b""
    tx = b""
    rx = bytearray()
    results: list[TransactionResult] = []
    try:
        with serial.Serial(args.port, args.baud, 8, "N", 1, timeout=0.1) as port:
            if args.await_banner:
                banner = wait_for_banner(port, args.banner_timeout)
            time.sleep(args.startup_delay)
            port.reset_input_buffer()
            for index, payload in enumerate(payloads):
                try:
                    result, run_tx, _ = transaction(
                        port,
                        payload,
                        args.seq + index,
                        args.timeout,
                        rx,
                        expected_timestep_prefix(payload)
                        if args.float_uart_test
                        else None,
                        b"NS" if args.float_uart_test else b"",
                    )
                except ProtocolError:
                    if args.capture_debug_log:
                        try:
                            debug_log = read_debug_log(port, args.report_timeout)
                        except (ProtocolError, TransportTimeout):
                            pass
                    raise
                tx += run_tx
                results.append(result)
            if args.float_uart_test:
                tx += check_nonfinite_input_rejected(
                    port, payloads[-1], args.seq + args.runs, args.timeout, rx
                )
                tx += check_nonfinite_output_rejected(
                    port, payloads[-1], args.seq + args.runs + 1, args.timeout, rx
                )
            if args.capture_timing_log:
                timing_log = read_timing_log(port, args.report_timeout)
                validate_timing_log(timing_log)
                timing_markdown = timing_report(timing_log, result)
            if args.capture_debug_log:
                debug_log = read_debug_log(port, args.report_timeout)
        summary.update(asdict(results[-1]), status="PASS")
        summary["seq"] = args.seq
        if args.float_uart_test:
            summary["result_check"] = "per_timestep_prefix_exact"
            summary["nonfinite_input_rejected"] = True
            summary["nonfinite_output_rejected"] = True
        if args.runs > 1:
            summary["last_seq"] = results[-1].seq
            summary["elapsed_seconds"] = sum(
                result.elapsed_seconds for result in results
            )
            summary["input_payload_crcs"] = [crc32(data) for data in payloads]
            summary["result_payload_crcs"] = [
                result.result_payload_crc for result in results
            ]
        if args.capture_timing_log:
            summary["profile_log_bytes"] = len(timing_log)
            summary["timing_report"] = "timing_report.md"
            summary["timing_validated"] = True
        if args.capture_debug_log:
            summary["debug_log_bytes"] = len(debug_log)
            summary["debug_validated"] = True
        if args.runs == 1:
            print(
                f"PASS seq={result.seq} cycles={result.cycles} hz={result.hz} "
                f"inference={result.inference_seconds:.6f}s "
                f"elapsed={result.elapsed_seconds:.3f}s"
            )
        else:
            elapsed = sum(item.elapsed_seconds for item in results)
            print(
                f"PASS runs={args.runs} seq={args.seq}..{results[-1].seq} "
                f"hz={results[-1].hz} elapsed={elapsed:.3f}s"
            )
        return 0
    except (OSError, ValueError, ProtocolError, TransportTimeout) as error:
        summary["error"] = str(error)
        if debug_log:
            summary["debug_log_bytes"] = len(debug_log)
            summary["debug_validated"] = True
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    finally:
        files = {
            "startup.bin": banner,
            "input.bin": payloads[0],
            "tx.bin": tx,
            "rx.bin": bytes(rx),
            "profile.log": timing_log,
            "debug.log": debug_log,
        }
        if args.runs > 1:
            files["inputs.bin"] = b"".join(payloads)
        if timing_markdown:
            files["timing_report.md"] = timing_markdown
        write_capture(
            args.log_dir,
            files,
            summary,
        )


if __name__ == "__main__":
    raise SystemExit(main())
