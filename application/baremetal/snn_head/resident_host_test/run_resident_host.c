#include "snn_head_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int load_resident_assets(void);
uint32_t resident_test_thread_id(const rvrt_artifact_t *);
int host_enum_output_axons(const uint8_t *, unsigned, uint32_t *, uint32_t,
                           uint32_t *);
void resident_test_start_inference(void);
uint64_t resident_test_startup_writes(void);
uint32_t resident_test_illegal_writes(void);
void resident_test_expect_config_frames(uint64_t);

#define CHECK(expr, message)                                                   \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "FAIL: %s at %d\n", message, __LINE__);            \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static const uint8_t *const starts[] = {
    snn_head_fc1_lif_artifact_start, snn_head_block0_lif_artifact_start,
    snn_head_block1_lif_artifact_start, snn_head_fc2_artifact_start,
    snn_head_fc3_artifact_start};
static const uint8_t *const sizes[] = {
    snn_head_fc1_lif_artifact_size, snn_head_block0_lif_artifact_size,
    snn_head_block1_lif_artifact_size, snn_head_fc2_artifact_size,
    snn_head_fc3_artifact_size};
static rvrt_frame_t roots[5];
static uint32_t threads[5], axons[5][1536], output_lcn[5];
static unsigned init_count, sync_count, layer, step, request;
static bool inject_failure;
static float input[SNN_HEAD_INPUT_FLOAT_COUNT],
    action[SNN_HEAD_ACTION_FLOAT_COUNT];

static rvrt_frame_t work(uint32_t kind, uint32_t axon, uint32_t time,
                         uint8_t value)
{
    return (rvrt_frame_t){kind | ((time >> 7) << 28),
                          ((time & 127) << 17) | (axon << 8) | value};
}

uint32_t resident_test_control(uint32_t high, uint32_t low, rvrt_frame_t *out,
                               uint32_t cap)
{
    if ((high >> 28) == 0xD) {
        if (init_count % 5)
            assert(step == 8);
        layer = init_count++ % 5;
        step = 0;
        assert(high == roots[layer].high && low == roots[layer].low);
        out[0] = (rvrt_frame_t){0xE0000000, threads[layer]};
        return 1;
    }
    assert((high & 0x0FFFFFFF) == (roots[layer].high & 0x0FFFFFFF));
    assert((low & 0xFFFFFF) == 1 && step < 8);
    ++sync_count;
    uint32_t n = 0;
    /* A real consumer must retain output after this early completion. */
    out[n++] = (rvrt_frame_t){0xE0000000, threads[layer]};
    const uint32_t elements = layer == 4 ? 7 : 1536;
    const uint32_t timestamp = step << output_lcn[layer];
    for (uint32_t e = 0; e < elements; ++e) {
        if (inject_failure && layer == 1 && step == 0 && e == elements - 1)
            continue;
        if (layer < 3) {
            out[n++] = work(0x80000000, axons[layer][e], timestamp,
                            (uint8_t)((e + step + request) & 1));
        } else {
            const uint32_t value = 1000 + 37 * request + step;
            for (int lane = 3; lane >= 0; --lane)
                out[n++] =
                    work(0xA0000000, axons[layer][e] + (uint32_t)lane * 8,
                         timestamp, (uint8_t)(value >> (lane * 8)));
        }
    }
    ++step;
    assert(n <= cap);
    return n;
}

int main(int argc, char **argv)
{
    inject_failure = argc > 1 && strcmp(argv[1], "fail") == 0;
    CHECK(load_resident_assets() == 0, "load exact asset bytes");
    uint64_t expected_config = 0;
    for (unsigned i = 0; i < 5; ++i) {
        rvrt_artifact_t artifact;
        const size_t bytes = snn_head_artifact_size(sizes[i]);
        CHECK(rvrt_artifact_read(starts[i], bytes, &artifact) ==
                  RVRT_ARTIFACT_OK,
              "reader accepts asset");
        uint32_t words = 0, elements = 0;
        CHECK(rvrt_artifact_config_word_count(&artifact, &words) ==
                  RVRT_ARTIFACT_OK,
              "config frame count available");
        expected_config += words / 2;
        CHECK(rvrt_build_init_frame(&artifact, 0, &roots[i]) ==
                  RVRT_CODEC_STATUS_OK,
              "root control frame");
        threads[i] = resident_test_thread_id(&artifact);
        rvrt_artifact_output_mapping_view_t mapping;
        CHECK(rvrt_artifact_get_output_mapping_view(
                  &artifact, 0, 0, &mapping) == RVRT_ARTIFACT_OK,
              "output mapping");
        output_lcn[i] = mapping.target_lcn;
        CHECK(host_enum_output_axons(starts[i], (unsigned)bytes, axons[i], 1536,
                                     &elements) == 0,
              "output addresses");
        CHECK(elements == (i == 4 ? 7U : 1536U), "output shape");
    }
    CHECK(!snn_head_run_chunk(input, action),
          "inference requires full initialization");
    resident_test_expect_config_frames(expected_config);
    CHECK(snn_head_initialize(), "all layers initialize");
    CHECK(resident_test_startup_writes() == expected_config,
          "each config loaded exactly once");
    CHECK(snn_head_initialize(), "initialization is idempotent");
    CHECK(resident_test_startup_writes() == expected_config,
          "idempotence sends no config");
    resident_test_start_inference();
    for (request = 0; request < (inject_failure ? 1U : 3U); ++request) {
        for (unsigned i = 0; i < SNN_HEAD_INPUT_FLOAT_COUNT; ++i)
            input[i] =
                (float)((int)(i % 37) - 18) * (request == 1 ? 0.25f : 0.5f);
        const bool ok = snn_head_run_chunk(input, action);
        if (inject_failure) {
            CHECK(!ok, "missing output stops request");
            CHECK(init_count == 2 && sync_count == 9,
                  "later layers never start after failure");
            CHECK(!snn_head_run_chunk(input, action),
                  "faulted resident model rejects another request");
            CHECK(init_count == 2, "retry sends no work after failure");
            break;
        }
        CHECK(ok, "resident chunk completes");
        CHECK(init_count == (request + 1) * 5 &&
                  sync_count == (request + 1) * 40,
              "each chunk resets all five timelines and runs eight steps");
        for (unsigned t = 0; t < 8; ++t)
            for (unsigned e = 0; e < 7; ++e) {
                const float expected = (float)(1000 + 37 * request + t) *
                                       snn_head_fc3_output_scale[e];
                CHECK(action[t * 7 + e] == expected,
                      "complete action retains timestep and lane identity");
            }
        const snn_head_profile_t *profile = snn_head_profile_get();
        for (unsigned i = 0; i < 5; ++i) {
            CHECK(profile->layers[i].config_frames == 0 &&
                      profile->layers[i].deploy_cycles == 0,
                  "warm timing excludes initialization and configuration");
            CHECK(profile->layers[i].complete_frames == 9,
                  "INIT and eight step barriers");
            CHECK(profile->layers[i].output_work_frames ==
                      (i < 3 ? 12288U : (i == 3 ? 49152U : 224U)),
                  "all explicit DATA and VOLTAGE lanes received");
        }
    }
    CHECK(resident_test_illegal_writes() == 0,
          "no configuration stream during inference");
    puts("PASS: resident lifecycle, exact output and chunk choreography");
    return 0;
}
