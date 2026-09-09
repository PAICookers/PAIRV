/*
 * End-to-end SNN Head board test: build this directory independently.
 *
 * Unlike the isolated layer tests, this program runs the real five-layer
 * workspace handoff. It compares the final action[8][7] against the generated
 * golden output on the board and prints one unambiguous GOLDEN PASS/FAIL line.
 * Build with DUMP=1 to emit additional layer-boundary values for diagnosis.
 */
#include <stdio.h>

#include "../profile_report.h"
#include "debug.h"
#include "snn_head.h"
#include "snn_head_golden.h"
#include "snn_head_golden_compare.h"

int main(void)
{
    rv_debug_set_level(RV_DEBUG_INFO);
    printf("== snn_head chain test [end-to-end] golden_ready=%d ==\r\n",
           snn_head_golden_ready);

    float action[SNN_HEAD_GOLDEN_ACTION_ELEMS] = {0};
    if (!snn_head_run_chunk(snn_head_golden_input, action)) {
        printf("chain: RUN FAILED\r\n");
        return 1;
    }

    const int result = snn_head_golden_compare_close_f32(
        "chain", action, snn_head_golden_fc3_out, SNN_HEAD_GOLDEN_ACTION_ELEMS);
    if (result == 0) {
        snn_head_board_profile_print();
    }
    return result;
}
