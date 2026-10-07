#ifndef TEST_MOCK_RUNTIME_HW_H
#define TEST_MOCK_RUNTIME_HW_H

#include <stdbool.h>
#include <stdint.h>

#include "transport_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

void mock_runtime_reset(void);
void mock_runtime_clear_sent(void);
void mock_runtime_queue_rx(const rvrt_frame_t *frames, uint32_t frame_count);
void mock_runtime_queue_rx_segments(const rvrt_frame_t *frames,
                                    uint32_t frame_count,
                                    const uint32_t *segment_frame_counts,
                                    uint32_t segment_count);
void mock_runtime_set_auto_irq(bool enabled);
void mock_runtime_set_irq_on_enable(bool enabled);
void mock_runtime_set_irq_on_disable(bool enabled);
void mock_runtime_set_fifo_read_error(bool enabled);
void mock_runtime_probe_rx_barrier_active(rvrt_transport_t *session);

uint32_t mock_runtime_sent_count(void);
const rvrt_frame_t *mock_runtime_sent_frames(void);
rvrt_runtime_status_t mock_runtime_nested_send_status(void);
rvrt_runtime_status_t mock_runtime_nested_sync_status(void);

#ifdef __cplusplus
}
#endif
#endif /* TEST_MOCK_RUNTIME_HW_H */
