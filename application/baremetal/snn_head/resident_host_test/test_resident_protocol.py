"""Pure resident record checks, including the board runner's rejection cases."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import snn_head_resident_protocol as protocol


PLAN_SHA256 = "a" * 64
BEFORE_AUDIT = (
    b"SNN_HEAD_RESIDENT_AUDIT phase=before_init status=ok cycles=10 windows=8450 "
    b"bytes=8468552 layer=0 query=0 frame=0 expected=0000000000000000 "
    b"actual=0000000000000000\r\n"
)
AFTER_AUDIT = (
    b"SNN_HEAD_RESIDENT_AUDIT phase=after_chunk_weights status=ok cycles=10 "
    b"windows=8158 bytes=8268288 layer=0 query=0 frame=0 "
    b"expected=0000000000000000 actual=0000000000000000\r\n"
)
STARTUP = (
    b"SNN_HEAD_RESIDENT_INIT status=ok cycles=20 hz=100000000 config_frames=99\r\n"
    + ("SNN_HEAD_RESIDENT_AUDIT_PLAN sha256=%s\r\n" % PLAN_SHA256).encode()
    + BEFORE_AUDIT
    + b"SNN_HEAD_UART_READY\r\n"
)


def resident_timing(**overrides):
    # These are fixed model counts, independent of the parser's constants.
    outputs = {"fc1_lif": 12288, "block0": 12288, "block1": 12288,
               "fc2": 49152, "fc3": 224}
    lines = ["SNN_HEAD_TIMING_BEGIN hz=100000000 chain_cycles=123"]
    for layer, count in outputs.items():
        fields = dict(config_frames=0, config_submit_cycles=0, deploy_cycles=0,
                      complete_frames=9, output_work_frames=count, rx_irq_count=9)
        fields.update(overrides.get(layer, {}))
        lines.append("SNN_HEAD_TIMING layer=%s %s" % (
            layer, " ".join("%s=%s" % item for item in fields.items())))
    lines.append("SNN_HEAD_TIMING_END")
    return ("\n".join(lines) + "\n").encode()


class ResidentProtocolTests(unittest.TestCase):
    def test_startup_returns_typed_counts_and_bound_plan(self):
        result = protocol.validate_startup(STARTUP, PLAN_SHA256)
        self.assertEqual(result["init"]["cycles"], 20)
        self.assertEqual(result["init"]["config_frames"], 99)
        self.assertEqual(result["plan_sha256"], PLAN_SHA256)
        self.assertEqual(result["before_init"]["bytes"], 8468552)
        self.assertEqual(result["before_init"]["windows"], 8450)

    def test_missing_before_audit_is_rejected(self):
        with self.assertRaisesRegex(protocol.RunFailure, "before_init"):
            protocol.validate_startup(STARTUP.replace(BEFORE_AUDIT, b""), PLAN_SHA256)

    def test_failed_init_is_rejected(self):
        with self.assertRaisesRegex(protocol.RunFailure, "initialization.*pass"):
            protocol.validate_startup(
                STARTUP.replace(b"status=ok", b"status=fail", 1), PLAN_SHA256)

    def test_startup_requires_matching_plan_and_one_ready(self):
        for wire, plan in (
            (STARTUP, "b" * 64),
            (STARTUP.replace(b"SNN_HEAD_UART_READY\r\n", b""), PLAN_SHA256),
            (STARTUP + b"SNN_HEAD_UART_READY\r\n", PLAN_SHA256),
        ):
            with self.subTest(wire=wire, plan=plan), self.assertRaises(protocol.RunFailure):
                protocol.validate_startup(wire, plan)

    def test_startup_rejects_duplicate_or_invalid_fields(self):
        for old, new in (
            (b"cycles=20", b"cycles=20 cycles=21"),
            (b"cycles=20", b"cycles=-1"),
            (b"hz=100000000", b"hz=0"),
            (b"config_frames=99", b"config_frames=0"),
        ):
            with self.subTest(new=new), self.assertRaises(protocol.RunFailure):
                protocol.validate_startup(STARTUP.replace(old, new), PLAN_SHA256)

    def test_after_audit_returns_typed_coverage(self):
        result = protocol.validate_audit(AFTER_AUDIT, "after_chunk_weights", 8268288)
        self.assertEqual(result["bytes"], 8268288)
        self.assertEqual(result["windows"], 8158)
        self.assertEqual(result["cycles"], 10)

    def test_failed_after_chunk_audit_is_rejected(self):
        with self.assertRaisesRegex(protocol.RunFailure, "after_chunk_weights.*PASS"):
            protocol.validate_audit(
                AFTER_AUDIT.replace(b"status=ok", b"status=fail"),
                "after_chunk_weights", 8268288)

    def test_audit_rejects_wrong_coverage_and_malformed_records(self):
        for wire in (
            AFTER_AUDIT.replace(b"bytes=8268288", b"bytes=8268287"),
            AFTER_AUDIT.replace(b"windows=8158", b"windows=0"),
            AFTER_AUDIT.replace(b"expected=0000000000000000", b"expected=xyz"),
            AFTER_AUDIT.replace(b"phase=after_chunk_weights", b"phase=before_init"),
            AFTER_AUDIT + AFTER_AUDIT,
            AFTER_AUDIT + b"\xff",
        ):
            with self.subTest(wire=wire), self.assertRaises(protocol.RunFailure):
                protocol.validate_audit(wire, "after_chunk_weights", 8268288)

    def test_timing_accepts_all_five_complete_layers(self):
        result = protocol.validate_resident_timing(resident_timing())
        self.assertEqual(len(result), 5)
        self.assertEqual(result["fc2"]["output_work_frames"], 49152)
        self.assertEqual(result["fc3"]["output_work_frames"], 224)

    def test_rejects_warm_configuration_or_deploy_work(self):
        for field in ("config_frames", "config_submit_cycles", "deploy_cycles"):
            with self.subTest(field=field), self.assertRaisesRegex(
                protocol.RunFailure, "warm resident.*zero"
            ):
                protocol.validate_resident_timing(resident_timing(fc2={field: 1}))

    def test_rejects_wrong_output_or_complete_count(self):
        for values in ({"output_work_frames": 49151}, {"complete_frames": 8}):
            with self.subTest(values=values), self.assertRaisesRegex(
                protocol.RunFailure, "frame count"
            ):
                protocol.validate_resident_timing(resident_timing(fc2=values))

    def test_timing_rejects_missing_duplicate_or_invalid_layers(self):
        wire = resident_timing()
        fc3 = next(line for line in wire.splitlines(keepends=True)
                   if line.startswith(b"SNN_HEAD_TIMING layer=fc3 "))
        for changed in (
            wire.replace(fc3, b""), wire + fc3,
            wire.replace(b"layer=fc3 ", b"layer=unknown "),
            resident_timing(fc2={"complete_frames": "invalid"}),
        ):
            with self.subTest(wire=changed), self.assertRaises(protocol.RunFailure):
                protocol.validate_resident_timing(changed)


if __name__ == "__main__":
    unittest.main()
