"""Validate the fixed three-chip SNN Head's resident text records.

These pure validators perform no UART, JTAG, Flash or filesystem operations.
They retain the checks used by the Scheme 1.0 board runner. The caller supplies
the expected canonical audit-plan hash after binding the plan to its ELF.

The timing validator checks resident frame/deployment counts only; use it with
``snn_head_uart_board_test.validate_timing_log`` for the full timing protocol.
Transport framing, deadlines and failure evidence remain the caller's concern.
"""

import re


BEFORE_AUDIT_BYTES = 8_465_120 + 143 * 24
AFTER_AUDIT_BYTES = 8_268_288
RESIDENT_OUTPUT_FRAMES = {
    "fc1_lif": 12_288,
    "block0": 12_288,
    "block1": 12_288,
    "fc2": 49_152,
    "fc3": 224,
}


class RunFailure(RuntimeError):
    """A resident record does not satisfy the fixed model's checks."""


def _fields(line, prefix):
    if not line.startswith(prefix + " "):
        raise RunFailure("invalid %s record" % prefix)
    fields = {}
    for token in line[len(prefix) + 1 :].split():
        if "=" not in token:
            raise RunFailure("invalid %s field" % prefix)
        key, value = token.split("=", 1)
        if not key or key in fields:
            raise RunFailure("duplicate %s field" % prefix)
        fields[key] = value
    return fields


def _one_line(data, prefix):
    try:
        lines = data.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise RunFailure("resident text record is not ASCII") from error
    matches = [line for line in lines if line.startswith(prefix + " ")]
    if len(matches) != 1:
        raise RunFailure("missing or duplicate %s" % prefix)
    return matches[0]


def _decimal(fields, names, record):
    values = {}
    for name in names:
        value = fields.get(name, "")
        if not value.isdigit():
            raise RunFailure("invalid %s %s" % (record, name))
        values[name] = int(value)
    return values


def validate_audit(data, phase, expected_bytes):
    prefix = "SNN_HEAD_RESIDENT_AUDIT"
    try:
        line = _one_line(data, prefix)
    except RunFailure as error:
        raise RunFailure("missing or duplicate %s audit" % phase) from error
    fields = _fields(line, prefix)
    expected_names = {
        "phase", "status", "cycles", "windows", "bytes", "layer", "query",
        "frame", "expected", "actual",
    }
    if set(fields) != expected_names or fields["phase"] != phase or fields["status"] != "ok":
        raise RunFailure("%s audit did not report the required PASS" % phase)
    numeric = _decimal(fields, ("cycles", "windows", "bytes", "layer", "query", "frame"), phase)
    if numeric["bytes"] != expected_bytes or numeric["windows"] <= 0:
        raise RunFailure("%s audit byte/window coverage differs" % phase)
    for name in ("expected", "actual"):
        if re.fullmatch(r"[0-9a-fA-F]{16}", fields[name]) is None:
            raise RunFailure("invalid %s audit %s" % (phase, name))
    return dict(fields, **numeric)


def validate_startup(data, expected_plan_sha256):
    init_prefix = "SNN_HEAD_RESIDENT_INIT"
    init_fields = _fields(_one_line(data, init_prefix), init_prefix)
    if set(init_fields) != {"status", "cycles", "hz", "config_frames"} or init_fields["status"] != "ok":
        raise RunFailure("resident initialization did not pass")
    init_numeric = _decimal(init_fields, ("cycles", "hz", "config_frames"), "resident init")
    if init_numeric["hz"] <= 0 or init_numeric["config_frames"] <= 0:
        raise RunFailure("resident initialization reported empty timing/config")
    plan_prefix = "SNN_HEAD_RESIDENT_AUDIT_PLAN"
    plan_fields = _fields(_one_line(data, plan_prefix), plan_prefix)
    if set(plan_fields) != {"sha256"} or plan_fields["sha256"] != expected_plan_sha256:
        raise RunFailure("resident audit plan identity differs from audit_plan.json")
    if data.count(b"SNN_HEAD_UART_READY\r\n") != 1:
        raise RunFailure("missing or duplicate resident UART ready marker")
    return {
        "init": dict(init_fields, **init_numeric),
        "plan_sha256": expected_plan_sha256,
        "before_init": validate_audit(data, "before_init", BEFORE_AUDIT_BYTES),
    }


def validate_resident_timing(data):
    """Require warm resident execution with exact output and completion counts."""
    try:
        lines = data.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise RunFailure("resident timing is not ASCII") from error
    observed = {}
    prefix = "SNN_HEAD_TIMING"
    for line in lines:
        if not line.startswith("SNN_HEAD_TIMING layer="):
            continue
        fields = _fields(line, prefix)
        layer = fields.get("layer", "")
        if layer not in RESIDENT_OUTPUT_FRAMES or layer in observed:
            raise RunFailure("missing, duplicate, or unexpected resident timing layer")
        numeric = _decimal(
            fields,
            ("config_frames", "config_submit_cycles", "deploy_cycles",
             "complete_frames", "output_work_frames"),
            "resident timing",
        )
        if any(numeric[name] != 0 for name in
               ("config_frames", "config_submit_cycles", "deploy_cycles")):
            raise RunFailure("warm resident config/deploy fields must remain zero")
        if (numeric["complete_frames"] != 9 or
                numeric["output_work_frames"] != RESIDENT_OUTPUT_FRAMES[layer]):
            raise RunFailure("resident output/COMPLETE frame count differs")
        observed[layer] = numeric
    if set(observed) != set(RESIDENT_OUTPUT_FRAMES):
        raise RunFailure("missing, duplicate, or unexpected resident timing layer")
    return observed
