#ifndef TEST_MOCK_RUNTIME_HW_H
#define TEST_MOCK_RUNTIME_HW_H

#include <stdbool.h>
#include <stdint.h>

#include "session.h"

void mock_runtime_reset(void);
void mock_runtime_queue_rx(const rvrt_frame_t *frames, uint32_t frame_count);
void mock_runtime_set_auto_irq(bool enabled);
void mock_runtime_probe_armed(rvrt_session_t *session);

uint32_t mock_runtime_sent_count(void);
const rvrt_frame_t *mock_runtime_sent_frames(void);
rvrt_session_status_t mock_runtime_nested_send_status(void);
rvrt_session_status_t mock_runtime_nested_sync_status(void);
uint32_t mock_runtime_task_run_count(void);

#endif /* TEST_MOCK_RUNTIME_HW_H */
