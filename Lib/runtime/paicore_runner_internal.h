#ifndef RVRT_PAICORE_RUNNER_INTERNAL_H
#define RVRT_PAICORE_RUNNER_INTERNAL_H

#include "paicore_runner.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Internal diagnostic exchange on an attached, configured runner. */
rvrt_session_status_t rvrt_paicore_runner_exchange_exact(
    rvrt_paicore_runner_t *runner, const rvrt_frame_t *request,
    uint32_t rx_goal, uint32_t timeout_ms,
    rvrt_session_rx_exact_frame_handler_t handler, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_PAICORE_RUNNER_INTERNAL_H */
