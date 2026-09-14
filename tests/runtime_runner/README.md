# Runtime runner host tests

These tests exercise the public runner lifecycle against the real artifact
reader, codec, session and session I/O with a synthetic four-element PBCA.
No model assets, application code, hardware, or upstream Python compiler are
required. `artifact_fixture.cpp` builds the fixture using the repository's
FlatBuffers binding; the NoC mock supplies deterministic control responses.

Run with a native toolchain:

```sh
cmake -S tests/runtime_runner -B build/runner-stats1 -DRVRT_TEST_STATS=1
cmake --build build/runner-stats1 -j
ctest --test-dir build/runner-stats1 --output-on-failure
```

Repeat with `-DRVRT_TEST_STATS=0`. AddressSanitizer and UndefinedBehaviorSanitizer
are always enabled. The suite covers independent preparation, exclusive IRQ
attachment, configuration reuse across detach, invalid transitions, sticky load
failure, deploy cleanup, voltage-state sizing, and preserved sparse zero output.
Tests inspect internal state only for explicit failure injection and ownership
assertions; applications must follow the public borrowing contract.

`test_exact` covers DATA and VOLTAGE unique-slot coverage, explicit zero DATA,
COMPLETE before/after outputs, lane reordering, metadata thread identity,
repeated inference, generic layouts, and missing/duplicate/wrong-kind/address/
time/identity failures. Its fixed response can exceed the RX scratch capacity.

Exact receive currently consumes the declared frame goal inside one IRQ. The
hardware FIFO API exposes no nonblocking availability query: a missing frame
may block an MMIO read, so the software timeout is not an IRQ deadline. The mock
returns a read error for that case; it does not prove hardware boundedness.
This behavior requires architecture review against the repository's bounded,
nonblocking interrupt rule and an external deadline for hardware experiments.

`test_exchange` verifies internal diagnostic transactions without a model audit:
raw E-prefix and WORK-prefix payloads, replies larger than RX scratch, unchanged
sync epochs, exact transport statistics, semantic completion boundaries, handler
failure, and missing replies. It inherits the blocking-FIFO limitation above.
