#ifndef RVRT_RUNTIME_SESSION_INTERNAL_H
#define RVRT_RUNTIME_SESSION_INTERNAL_H

#include "runtime_session.h"

#ifdef __cplusplus
extern "C" {
#endif

rvrt_runtime_status_t
rvrt_runtime_session_begin(rvrt_runtime_session_t *session, void *runner);

void rvrt_runtime_session_end(rvrt_runtime_session_t *session, void *runner);

void rvrt_runtime_session_note_status(rvrt_runtime_session_t *session,
                                      rvrt_runtime_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_RUNTIME_SESSION_INTERNAL_H */
