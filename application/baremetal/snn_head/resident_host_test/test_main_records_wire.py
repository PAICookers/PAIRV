#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path


PLAN_SHA256 = "0123456789abcdef" * 4


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_main_records_wire.py PRODUCER")
    snn_head_dir = Path(__file__).resolve().parent.parent
    sys.path.insert(0, str(snn_head_dir / "tools"))
    from snn_head_resident_protocol import (
        AFTER_AUDIT_BYTES,
        BEFORE_AUDIT_BYTES,
        validate_audit,
        validate_startup,
    )

    wire = subprocess.run(
        [sys.argv[1], "--records"], check=True, stdout=subprocess.PIPE
    ).stdout
    ready = b"SNN_HEAD_UART_READY\r\n"
    ready_end = wire.index(ready) + len(ready)
    startup_wire = wire[:ready_end]
    after_wire = wire[ready_end:]
    startup = validate_startup(startup_wire, PLAN_SHA256)
    after = validate_audit(after_wire, "after_chunk_weights", AFTER_AUDIT_BYTES)

    assert startup["before_init"]["windows"] == 8450
    assert startup["before_init"]["bytes"] == BEFORE_AUDIT_BYTES == 8_468_552
    assert after["windows"] == 8158
    assert after["bytes"] == AFTER_AUDIT_BYTES == 8_268_288
    sys.stdout.buffer.write(wire)
    print("snn_head_main_records_wire: validated")


if __name__ == "__main__":
    main()
