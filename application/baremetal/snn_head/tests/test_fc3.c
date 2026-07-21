/*
 * Per-layer host test: fc3 (quant -> fc3 -> voltage readout -> action[7]).
 *
 * The fc3 fixture is not yet exported. This test self-skips (PENDING, exit 0)
 * until tests/runtime/fixtures/artifacts2/fc3/runtime/compile_artifacts.bin
 * exists, then automatically enforces the contract below.
 */
#include "snn_head_layer_test_common.h"

int main(void)
{
    static const snn_head_layer_contract_t contract = {
        "fc3",
        "fc3",
        1536U,                      /* input_entries  */
        8U,                         /* input_bit_width */
        7U,                         /* output_entries  (action_dim) */
        7U,                         /* output_elements */
        RVRT_OUTPUT_VOLTAGE,        /* int32 accumulator */
        SNN_HEAD_TEST_DTYPE_INT32,  /* 4 lanes / value */
    };
    return snn_head_run_layer_test(&contract, 1); /* skip_if_missing */
}
