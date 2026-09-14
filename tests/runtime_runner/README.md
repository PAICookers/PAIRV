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
