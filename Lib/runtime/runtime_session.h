#ifndef RVRT_RUNTIME_SESSION_H
#define RVRT_RUNTIME_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "transport_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Borrowed resources used to open one shared runtime session. */
typedef struct rvrt_runtime_session_open_config_s {
    /** Artifact bytes; they must remain alive and aligned while open. */
    const uint8_t *artifact_data;
    /** Number of valid bytes in artifact_data. */
    size_t artifact_size;
    /** Caller-owned storage used for input chunks and RX barrier frames. */
    rvrt_frame_t *frame_buffer;
    /** Number of rvrt_frame_t entries in frame_buffer. */
    uint32_t frame_capacity;
} rvrt_runtime_session_open_config_t;

/**
 * @brief Shared physical PAICORE transport/configuration owner.
 *
 * The structure is caller allocated and must be zero-initialized before its
 * first open call. Artifact bytes and frame storage are borrowed; they must
 * remain valid until rvrt_runtime_session_close().
 * Hardware operations are serialized and only one RX barrier is active.
 */
typedef struct rvrt_runtime_session_s {
    rvrt_artifact_view_t artifact;
    rvrt_transport_t transport;
    void *active_runner;
    uint32_t runner_count;
    bool opened;
    bool configured;
    bool faulted;
} rvrt_runtime_session_t;

/** @brief Open and verify an artifact-backed shared transport owner. */
rvrt_runtime_status_t
rvrt_runtime_session_open(rvrt_runtime_session_t *session,
                          const rvrt_runtime_session_open_config_t *config);

/** @brief Submit global config frames once; repeated calls are idempotent. */
rvrt_runtime_status_t
rvrt_runtime_session_configure(rvrt_runtime_session_t *session);

/**
 * @brief Close a shared session after all runners and barriers are inactive.
 *
 * Returns RVRT_RUNTIME_BUSY while a runner or RX barrier is still attached.
 */
rvrt_runtime_status_t
rvrt_runtime_session_close(rvrt_runtime_session_t *session);

/** @brief Read transport statistics accumulated by the shared session. */
rvrt_runtime_status_t
rvrt_runtime_session_get_stats(const rvrt_runtime_session_t *session,
                               rvrt_runtime_stats_t *stats);

/** @brief Return a diagnostic string for a runtime status. */
const char *rvrt_runtime_session_status_string(rvrt_runtime_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_RUNTIME_SESSION_H */
