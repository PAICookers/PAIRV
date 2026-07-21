/* Per-layer host test: fc2 (LN2 -> quant -> fc2 -> li_out voltage readout). */
#include "snn_head_layer_test_common.h"

int main(void)
{
    static const snn_head_layer_contract_t contract = {
        "fc2",
        "fc2",
        1536U,                      /* input_entries  */
        8U,                         /* input_bit_width */
        1536U,                      /* output_entries  */
        1536U,                      /* output_elements */
        RVRT_OUTPUT_VOLTAGE,        /* int32 membrane  */
        SNN_HEAD_TEST_DTYPE_INT32,  /* 4 lanes / value */
    };
    return snn_head_run_layer_test(&contract, 0);
}
