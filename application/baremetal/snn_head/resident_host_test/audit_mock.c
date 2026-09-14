#include "snn_head_audit_plan.h"
#include "snn_head_internal.h"
#include <assert.h>

static bool corrupt;
static uint32_t cursor;
static rvrt_artifact_t artifacts[5];
static bool loaded[5];
void resident_test_audit_fault(bool enabled) { corrupt = enabled; }

uint32_t resident_test_audit(uint32_t high, uint32_t low, rvrt_frame_t *out)
{
    static const uint8_t *const starts[] = {
        snn_head_fc1_lif_artifact_start, snn_head_block0_lif_artifact_start,
        snn_head_block1_lif_artifact_start, snn_head_fc2_artifact_start,
        snn_head_fc3_artifact_start};
    static const uint8_t *const sizes[] = {
        snn_head_fc1_lif_artifact_size, snn_head_block0_lif_artifact_size,
        snn_head_block1_lif_artifact_size, snn_head_fc2_artifact_size,
        snn_head_fc3_artifact_size};
    const uint32_t count =
        sizeof(snn_head_audit_queries) / sizeof(snn_head_audit_queries[0]);
    uint32_t attempts = 0;
    while (snn_head_audit_queries[cursor].request.high != high ||
           snn_head_audit_queries[cursor].request.low != low) {
        cursor = (cursor + 1U) % count;
        assert(++attempts < count);
    }
    unsigned layer = 0;
    while (layer + 1U < 5U && cursor >= snn_head_audit_ranges[layer + 1U][0])
        ++layer;
    const snn_head_audit_query_t *const query = &snn_head_audit_queries[cursor];
    if (!loaded[layer]) {
        assert(rvrt_artifact_read(starts[layer],
                                  snn_head_artifact_size(sizes[layer]),
                                  &artifacts[layer]) == RVRT_ARTIFACT_OK);
        loaded[layer] = true;
    }
    out[0] = query->response;
    for (uint32_t i = 0; i < query->payload_frames; ++i) {
        assert(rvrt_artifact_config_frame_words(
                   &artifacts[layer], query->expected_first_frame + i,
                   &out[i + 1].high, &out[i + 1].low) == RVRT_ARTIFACT_OK);
    }
    if (corrupt && cursor == 2U)
        out[3].low ^= 1U;
    cursor = (cursor + 1U) % count;
    return (uint32_t)query->payload_frames + 1U;
}
