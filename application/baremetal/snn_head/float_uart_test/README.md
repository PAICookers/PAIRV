# SNN Head Float UART Test

This standalone board target validates the exact UART implementation used by
the production SNN Head demo without linking PAICore artifacts, runtime code,
or model operators. Every transaction still receives all `8 * 768 = 6144`
float32 input values. Its callback maps `output[t][j] = input[t][j]` for all 8
timesteps and 7 output columns, allowing the host to compare all 224 RESULT
bytes exactly without reducing the production input payload.

Build from the repository root:

```sh
source setup.sh
make CORE=n307fd DOWNLOAD=ilm \
  PROGRAM=application/baremetal/snn_head/float_uart_test clean all
```

For a board run, start the host before upload so the SDK Banner and ready marker
are captured:

```sh
RUN_DIR="runs/snn_head_float_uart_test_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RUN_DIR"
python3 application/baremetal/snn_head/tools/snn_head_uart_board_test.py \
  --port "<stable-serial-device>" --await-banner \
  --float-uart-test --runs 100 --timeout 10 \
  --log-dir "$RUN_DIR" &
client_pid=$!
make CORE=n307fd DOWNLOAD=ilm \
  PROGRAM=application/baremetal/snn_head/float_uart_test \
  OPENOCD="$PWD/openocd-linux-x64-4427ee7/bin/openocd" upload
upload_status=$?
wait "$client_pid"
client_status=$?
exit $((upload_status != 0 ? upload_status : client_status))
```

Acceptance requires a byte-exact per-timestep prefix RESULT, successful
resynchronization from an overlapping `NS` prefix, a `BAD_PAYLOAD` response to a
valid-CRC INPUT containing NaN, and an `INFERENCE_FAILED` response when the
test-only finite sentinel `12345.0f` makes the callback produce infinity. The
host saves the Banner, combined TX/RX bytes, input, and summary. This target
does not exercise PAICore or the SNN Head model chain.

`--runs 100` keeps one serial connection open and tests 100 deterministic
random tensors using seeds `seed..seed+99`. `inputs.bin` stores all 100 inputs;
`summary.json` records every input and RESULT CRC.
