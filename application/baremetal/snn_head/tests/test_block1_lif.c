/* Per-layer host test: block1_lif (LN -> quant -> fc -> LIF, subtract reset). */
#include "snn_head_layer_test_common.h"

int main(void)
{
    static const snn_head_layer_contract_t contract = {
        "block1_lif",
        "block1_lif",
        1536U,                      /* input_entries  */
        8U,                         /* input_bit_width */
        1536U,                      /* output_entries  */
        1536U,                      /* output_elements */
        RVRT_OUTPUT_DATA,           /* spikes          */
        SNN_HEAD_TEST_DTYPE_UINT1,  /* 1-bit           */
    };
    return snn_head_run_layer_test(&contract, 0);
}
