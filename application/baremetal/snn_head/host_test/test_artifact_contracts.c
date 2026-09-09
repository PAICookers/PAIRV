/*
 * Host-only artifact contract and input-frame budget checks for all five
 * layers. Numerical inference remains covered by the board layer/chain tests.
 */
#include "artifact_contract_test.h"

int main(void)
{
    /* These values are the application assumptions validated against assets.
     */
    static const snn_head_layer_contract_t contracts[] = {
        {"fc1_lif", "fc1_lif", 768U, 8U, 1536U, 1536U, RVRT_OUTPUT_DATA,
         SNN_HEAD_TEST_DTYPE_UINT1},
        {"block0_lif", "block0_lif", 1536U, 8U, 1536U, 1536U, RVRT_OUTPUT_DATA,
         SNN_HEAD_TEST_DTYPE_UINT1},
        {"block1_lif", "block1_lif", 1536U, 8U, 1536U, 1536U, RVRT_OUTPUT_DATA,
         SNN_HEAD_TEST_DTYPE_UINT1},
        {"fc2", "fc2", 1536U, 8U, 1536U, 1536U, RVRT_OUTPUT_VOLTAGE,
         SNN_HEAD_TEST_DTYPE_INT32},
        {"fc3", "fc3", 1536U, 8U, 7U, 7U, RVRT_OUTPUT_VOLTAGE,
         SNN_HEAD_TEST_DTYPE_INT32},
    };
    int failures = 0;

    for (size_t i = 0U; i < sizeof(contracts) / sizeof(contracts[0]); ++i) {
        failures += snn_head_run_layer_test(&contracts[i]) != 0;
    }

    if (failures == 0) {
        puts("snn head layer contract tests passed");
        return 0;
    }
    printf("snn head layer contract tests failed: %d\n", failures);
    return 1;
}
