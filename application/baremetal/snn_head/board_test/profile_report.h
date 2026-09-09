#ifndef SNN_HEAD_BOARD_PROFILE_REPORT_H
#define SNN_HEAD_BOARD_PROFILE_REPORT_H

#include <stdio.h>

#include "nuclei_sdk_soc.h"
#include "snn_head_profile.h"

static bool snn_head_board_profile_write(void *ctx, const char *data,
                                         size_t length)
{
    (void)ctx;
    return fwrite(data, 1U, length, stdout) == length;
}

static void snn_head_board_profile_print(void)
{
    (void)snn_head_profile_write_report(snn_head_board_profile_write, NULL,
                                        (uint32_t)SystemCoreClock, 0U, NULL);
}

#endif /* SNN_HEAD_BOARD_PROFILE_REPORT_H */
