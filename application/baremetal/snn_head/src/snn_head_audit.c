#include "snn_head_audit.h"
#include "paicore_runner_internal.h"
#include "snn_head_audit_plan.h"

#include <string.h>

static snn_head_audit_stats_t stats;
static rv_counter_t audit_started;

typedef struct {
    const rvrt_artifact_t *artifact;
    const snn_head_audit_query_t *query;
    uint32_t received;
    uint32_t compare_start;
    bool mismatch;
} audit_receive_t;

const snn_head_audit_stats_t *snn_head_audit_stats(void) { return &stats; }
const char *snn_head_audit_identity(void)
{
    return snn_head_audit_plan_identity;
}

void snn_head_audit_begin(void)
{
    memset(&stats, 0, sizeof(stats));
    audit_started = __get_rv_cycle();
}

void snn_head_audit_end(bool passed)
{
    stats.cycles = __get_rv_cycle() - audit_started;
    stats.passed = passed;
}

static rvrt_session_status_t receive(void *user_data, const rvrt_frame_t *frame,
                                     bool *complete)
{
    audit_receive_t *const state = user_data;
    const uint32_t index = state->received++;
    rvrt_frame_t expected = state->query->response;
    bool readable = true;
    if (index > 0U) {
        readable = rvrt_artifact_config_frame_words(
                       state->artifact,
                       state->query->expected_first_frame + index - 1U,
                       &expected.high, &expected.low) == RVRT_ARTIFACT_OK;
    }
    const bool compare = index == 0U || index > state->compare_start;
    if (compare && (!readable || expected.high != frame->high ||
                    expected.low != frame->low)) {
        if (!state->mismatch) {
            stats.failed_frame = index;
            stats.expected_high = expected.high;
            stats.expected_low = expected.low;
            stats.actual_high = frame->high;
            stats.actual_low = frame->low;
        }
        state->mismatch = true;
    }
    if (compare && index > 0U)
        ++stats.compared_frames;
    /* Consume the fixed response window after a mismatch before faulting. */
    *complete = state->received == (uint32_t)state->query->payload_frames + 1U;
    return *complete && state->mismatch ? RVRT_SESSION_RUNTIME_ERROR
                                        : RVRT_SESSION_OK;
}

bool snn_head_audit_layer(rvrt_paicore_runner_t *runner,
                          const rvrt_artifact_t *artifact, unsigned layer,
                          bool immutable_only)
{
    if (layer >= 5U || runner == NULL || artifact == NULL)
        return false;
    const uint32_t first = snn_head_audit_ranges[layer][0];
    const uint32_t count = snn_head_audit_ranges[layer][1];
    for (uint32_t offset = 0; offset < count; ++offset) {
        const snn_head_audit_query_t *const query =
            &snn_head_audit_queries[first + offset];
        if (immutable_only && query->immutable_start == query->payload_frames)
            continue;
        audit_receive_t state = {
            .artifact = artifact,
            .query = query,
            .compare_start = immutable_only ? query->immutable_start : 0U,
        };
        stats.failed_layer = layer;
        stats.failed_query = offset;
        if (rvrt_paicore_runner_exchange_exact(
                runner, &query->request,
                (uint32_t)query->payload_frames + 1U, 1000U, receive,
                                               &state) != RVRT_SESSION_OK)
            return false;
        ++stats.windows;
    }
    return true;
}
