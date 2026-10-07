#include "runtime_session.h"

#include <string.h>

#include "runtime_session_internal.h"

rvrt_runtime_status_t
rvrt_runtime_session_open(rvrt_runtime_session_t *session,
                          const rvrt_runtime_session_open_config_t *config)
{
    if ((session == NULL) || (config == NULL) || session->opened ||
        (config->artifact_data == NULL) || (config->artifact_size == 0U) ||
        (config->frame_buffer == NULL) || (config->frame_capacity == 0U)) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    memset(session, 0, sizeof(*session));
    if (rvrt_artifact_read(config->artifact_data, config->artifact_size,
                           &session->artifact) != RVRT_ARTIFACT_OK) {
        memset(session, 0, sizeof(*session));
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    rvrt_artifact_info_t info = {0};
    if ((rvrt_artifact_get_info(&session->artifact, &info) !=
         RVRT_ARTIFACT_OK) ||
        (info.thread_count == 0U)) {
        memset(session, 0, sizeof(*session));
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }

    const rvrt_transport_config_t transport_config = {
        .artifact = &session->artifact,
        .rx_frames = config->frame_buffer,
        .rx_capacity = config->frame_capacity,
    };
    const rvrt_runtime_status_t status =
        rvrt_transport_init(&session->transport, &transport_config);
    if (status != RVRT_RUNTIME_OK) {
        memset(session, 0, sizeof(*session));
        return status;
    }

    session->opened = true;
    return RVRT_RUNTIME_OK;
}

rvrt_runtime_status_t
rvrt_runtime_session_configure(rvrt_runtime_session_t *session)
{
    if ((session == NULL) || !session->opened) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (session->faulted || session->transport.faulted) {
        session->faulted = true;
        return RVRT_RUNTIME_FAULTED;
    }
    if (session->configured) {
        return RVRT_RUNTIME_OK;
    }

    const rvrt_runtime_status_t status =
        rvrt_transport_load_config(&session->transport);
    if (status == RVRT_RUNTIME_OK) {
        session->configured = true;
    }
    return status;
}

rvrt_runtime_status_t
rvrt_runtime_session_close(rvrt_runtime_session_t *session)
{
    if (session == NULL) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (!session->opened) {
        return RVRT_RUNTIME_OK;
    }
    if ((session->runner_count != 0U) || (session->active_runner != NULL) ||
        session->transport.rx_barrier.active) {
        return RVRT_RUNTIME_BUSY;
    }

    const rvrt_runtime_status_t status =
        rvrt_transport_deinit(&session->transport);
    if (status == RVRT_RUNTIME_OK) {
        memset(session, 0, sizeof(*session));
    }
    return status;
}

rvrt_runtime_status_t
rvrt_runtime_session_get_stats(const rvrt_runtime_session_t *session,
                               rvrt_runtime_stats_t *stats)
{
    if ((session == NULL) || !session->opened) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    return rvrt_transport_get_stats(&session->transport, stats);
}

const char *rvrt_runtime_session_status_string(rvrt_runtime_status_t status)
{
    return rvrt_transport_status_string(status);
}

rvrt_runtime_status_t
rvrt_runtime_session_begin(rvrt_runtime_session_t *session, void *runner)
{
    if ((session == NULL) || (runner == NULL) || !session->opened ||
        !session->configured) {
        return RVRT_RUNTIME_RUNTIME_ERROR;
    }
    if (session->faulted || session->transport.faulted) {
        session->faulted = true;
        return RVRT_RUNTIME_FAULTED;
    }
    if (session->active_runner != NULL) {
        return RVRT_RUNTIME_BUSY;
    }
    session->active_runner = runner;
    return RVRT_RUNTIME_OK;
}

void rvrt_runtime_session_end(rvrt_runtime_session_t *session, void *runner)
{
    if ((session != NULL) && (session->active_runner == runner)) {
        session->active_runner = NULL;
    }
}

void rvrt_runtime_session_note_status(rvrt_runtime_session_t *session,
                                      rvrt_runtime_status_t status)
{
    if (session == NULL) {
        return;
    }
    if ((status == RVRT_RUNTIME_TIMEOUT) || (status == RVRT_RUNTIME_OVERFLOW) ||
        (status == RVRT_RUNTIME_HARDWARE_ERROR) ||
        (status == RVRT_RUNTIME_FAULTED) || session->transport.faulted) {
        session->faulted = true;
    }
}
