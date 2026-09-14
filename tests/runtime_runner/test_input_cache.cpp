#include "generated/compile_artifacts_generated.h"

extern "C" {
#include "frame_codec.h"
#include "frame_codec_internal.h"
#include "paicore_runner.h"
#include "session_io_internal.h"
}

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace fbs = paibox::backendv2::generated::fbs;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::fprintf(stderr, "FAIL: %s\n", label);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static_assert(sizeof(rvrt_artifact_input_entry_t) == 48U,
              "the bounded cache budget requires 48-byte input entries");

typedef uint32_t (*host_mock_gen_fn)(void *, uint32_t, uint32_t, rvrt_frame_t *,
                                     uint32_t);
extern "C" void host_mock_set_generator(host_mock_gen_fn fn, void *ctx);
extern "C" uint32_t host_mock_sent_count(void);

static std::vector<uint8_t> build_input_artifact(uint32_t element_count,
                                                 uint32_t cover_count)
{
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);
    const auto zero_core = fbs::CreateCoreOffset(builder, 0, 0, 0);
    const auto zero_copy = fbs::CreateCopyCount(builder, 0, 0, 0);
    const auto ticks = fbs::CreateTickParams(builder, 0, 8, 0);
    std::vector<flatbuffers::Offset<fbs::InputEntry>> entries;
    entries.reserve(static_cast<size_t>(element_count) * cover_count);
    for (uint32_t element = 0U; element < element_count; ++element) {
        for (uint32_t cover = 0U; cover < cover_count; ++cover) {
            const auto core = fbs::CreateCoreOffset(
                builder, 0, static_cast<int32_t>(cover), 0);
            entries.push_back(fbs::CreateInputEntry(
                builder, element, core, zero_copy, element / 64U,
                (element % 64U) * 8U, 5U, 0U, fbs::DataType_INT8));
        }
    }
    const std::vector<int32_t> input_dims = {
        static_cast<int32_t>(element_count)};
    const auto input_shape = fbs::CreateShapeDirect(builder, &input_dims);
    const auto input = fbs::CreateInputTensorMappingDirect(
        builder, "input", input_shape, 8, ticks, &entries);
    const std::vector<flatbuffers::Offset<fbs::InputTensorMapping>> inputs = {
        input};
    const auto input_mappings =
        fbs::CreateInputTensorMappingsDirect(builder, &inputs);

    const std::vector<int32_t> output_dims = {1};
    const auto output_shape = fbs::CreateShapeDirect(builder, &output_dims);
    const std::vector<flatbuffers::Offset<fbs::OutputEntry>> output_entries = {
        fbs::CreateOutputEntry(builder, 0U, 0U, 0U, fbs::DataType_UINT1)};
    const auto output = fbs::CreateOutputTensorMappingDirect(
        builder, "output", output_shape, fbs::OutputKind_DATA, 1, ticks,
        &output_entries);
    const std::vector<flatbuffers::Offset<fbs::OutputTensorMapping>> outputs = {
        output};
    const auto output_mappings =
        fbs::CreateOutputTensorMappingsDirect(builder, 1U, &outputs);
    const auto runtime =
        fbs::CreateRuntimeParams(builder, 8U, 1U, 8U, fbs::DecodeMode_STREAM);
    const std::vector<flatbuffers::Offset<fbs::CoreTick>> core_ticks;
    const auto thread = fbs::CreateThreadIOMappingDirect(
        builder, 0U, zero_core, runtime, input_mappings, output_mappings,
        &core_ticks);
    const std::vector<flatbuffers::Offset<fbs::ThreadIOMapping>> threads = {
        thread};
    const auto io = fbs::CreateIOMappingDirect(builder, &threads);
    const std::vector<uint32_t> config_words = {0U, 0U};
    const auto config = fbs::CreateConfigFramesDirect(
        builder, &config_words, fbs::WordOrder_HIGH_FIRST);
    const auto root = fbs::CreateCompileArtifacts(builder, 1U, io, config);
    fbs::FinishCompileArtifactsBuffer(builder, root);
    return {builder.GetBufferPointer(),
            builder.GetBufferPointer() + builder.GetSize()};
}

static int prepare_runner(const std::vector<uint8_t> &asset,
                          rvrt_paicore_runner_t *runner)
{
    static rvrt_voltage_decode_state_t voltage_state[8U * 1536U];
    const rvrt_paicore_runner_prepare_config_t config = {
        .artifact_data = asset.data(),
        .artifact_size = asset.size(),
        .voltage_state = voltage_state,
        .voltage_state_capacity = 8U * 1536U,
        .timeout_ms = 1U,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_SPARSE,
    };
    return rvrt_paicore_runner_prepare(runner, &config) == RVRT_SESSION_OK ? 0
                                                                           : 1;
}

static int compare_one(const rvrt_paicore_runner_t &runner, uint32_t timestep,
                       const std::vector<uint8_t> &input, uint32_t capacity)
{
    std::vector<rvrt_frame_t> generic(capacity);
    std::vector<rvrt_frame_t> cached(capacity);
    rvrt_input_cursor_t generic_cursor;
    rvrt_input_cursor_t cached_cursor;
    rvrt_input_cursor_init(&generic_cursor, timestep);
    rvrt_input_cursor_init(&cached_cursor, timestep);

    for (;;) {
        uint32_t generic_count = 0U;
        uint32_t cached_count = 0U;
        const rvrt_codec_status_t generic_status = rvrt_encode_input_chunk(
            &runner.input_view, &generic_cursor, input.data(), input.size(),
            generic.data(), capacity, &generic_count);
        const rvrt_codec_status_t cached_status =
            rvrt_encode_input_canonical_chunk(
                runner.fast_input_prototypes, runner.fast_input_cover_count,
                runner.input_view.element_count, runner.input_view.bit_width,
                &cached_cursor, input.data(), input.size(), cached.data(),
                capacity, &cached_count);
        CHECK(cached_status == generic_status, "cached/generic status differs");
        CHECK(cached_count == generic_count,
              "cached/generic frame count differs");
        CHECK(cached_cursor.entry_index == generic_cursor.entry_index,
              "cached/generic cursor differs");
        CHECK(cached_cursor.timestep == generic_cursor.timestep,
              "cached/generic cursor timestep differs");
        CHECK(std::memcmp(cached.data(), generic.data(),
                          generic_count * sizeof(rvrt_frame_t)) == 0,
              "cached/generic frame bits differ");
        if (generic_status == RVRT_CODEC_STATUS_DONE) {
            break;
        }
        CHECK(generic_status == RVRT_CODEC_STATUS_BUFFER_FULL,
              "unexpected generic codec status");
    }
    return 0;
}

static int verify_synthetic_mappings_bit_exact(void)
{
    struct Layer {
        uint32_t elements;
        uint32_t covers;
        uint32_t target_lcn;
    };
    static const Layer layers[] = {
        {65U, 1U, 5U},
        {768U, 2U, 5U},
        {1536U, 3U, 5U},
        {1536U, 8U, 5U},
    };
    static const uint32_t capacities[] = {1U, 2U,   3U,   7U,  8U,
                                          9U, 255U, 256U, 257U};

    for (const Layer &layer : layers) {
        const std::vector<uint8_t> asset =
            build_input_artifact(layer.elements, layer.covers);
        CHECK(!asset.empty(), "build canonical synthetic mapping");
        rvrt_paicore_runner_t runner = {};
        CHECK(prepare_runner(asset, &runner) == 0, "prepare synthetic mapping");
        CHECK(runner.has_fast_input_layout, "synthetic mapping is canonical");
        CHECK(runner.fast_input_cover_count == layer.covers,
              "synthetic mapping cover count");
        CHECK(runner.input_view.element_count == layer.elements,
              "synthetic mapping element count");
        for (uint32_t cover = 0U; cover < layer.covers; ++cover) {
            CHECK(runner.fast_input_prototypes[cover].target_lcn ==
                      layer.target_lcn,
                  "synthetic mapping input target LCN");
        }

        std::vector<uint8_t> input(layer.elements);
        for (uint32_t element = 0U; element < layer.elements; ++element) {
            static const uint8_t values[] = {0U, 1U, 127U, 128U, 255U};
            input[element] = values[element % 5U];
        }
        for (uint32_t timestep = 0U; timestep < 8U; ++timestep) {
            for (uint32_t capacity : capacities) {
                CHECK(compare_one(runner, timestep, input, capacity) == 0,
                      "synthetic cached/generic comparison");
            }
        }
        std::fill(input.begin(), input.end(), 0U);
        for (uint32_t capacity : capacities) {
            CHECK(compare_one(runner, 7U, input, capacity) == 0,
                  "all-zero cached/generic comparison");
        }
        CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
              "release synthetic mapping runner");
    }
    return 0;
}

static fbs::InputEntry *mutable_input_entry(std::vector<uint8_t> &asset,
                                            uint32_t index)
{
    rvrt_artifact_t parsed = {};
    if (rvrt_artifact_read(asset.data(), asset.size(), &parsed) !=
        RVRT_ARTIFACT_OK) {
        return nullptr;
    }
    auto *io = const_cast<fbs::IOMapping *>(
        static_cast<const fbs::IOMapping *>(parsed.io_mapping));
    if ((io == nullptr) || (io->threads() == nullptr) ||
        io->threads()->empty()) {
        return nullptr;
    }
    auto *thread = const_cast<fbs::ThreadIOMapping *>(io->threads()->Get(0));
    auto *mappings =
        thread == nullptr
            ? nullptr
            : const_cast<fbs::InputTensorMappings *>(thread->input_mappings());
    if ((mappings == nullptr) || (mappings->items() == nullptr) ||
        mappings->items()->empty()) {
        return nullptr;
    }
    auto *mapping =
        const_cast<fbs::InputTensorMapping *>(mappings->items()->Get(0));
    if ((mapping == nullptr) || (mapping->entries() == nullptr) ||
        index >= mapping->entries()->size()) {
        return nullptr;
    }
    return const_cast<fbs::InputEntry *>(mapping->entries()->Get(index));
}

static int expect_generic_fallback(std::vector<uint8_t> asset, uint32_t index,
                                   bool (*mutate)(fbs::InputEntry *),
                                   const char *label)
{
    fbs::InputEntry *entry = mutable_input_entry(asset, index);
    CHECK(entry != nullptr, "find mutable input entry");
    CHECK(mutate(entry), "mutate input entry");
    rvrt_paicore_runner_t runner = {};
    CHECK(prepare_runner(asset, &runner) == 0, label);
    CHECK(!runner.has_fast_input_layout, "noncanonical mapping falls back");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release fallback runner");
    return 0;
}

static bool duplicate_element(fbs::InputEntry *entry)
{
    return entry->mutate_elem_idx(2U);
}

static bool change_copy_id(fbs::InputEntry *entry)
{
    return entry->mutate_copy_id(1U);
}

static bool change_tick(fbs::InputEntry *entry)
{
    return entry->mutate_tick_relative(1U);
}

static bool change_axon(fbs::InputEntry *entry)
{
    return entry->mutate_addr_axon(16U);
}

static bool change_dtype(fbs::InputEntry *entry)
{
    return entry->mutate_dtype(fbs::DataType_UINT8);
}

static bool change_valid_route(fbs::InputEntry *entry)
{
    auto *offset = const_cast<fbs::CoreOffset *>(entry->core_offset());
    return (offset != nullptr) && offset->mutate_x(offset->x() + 1);
}

static bool change_target_lcn(fbs::InputEntry *entry)
{
    return entry->mutate_target_lcn(4U);
}

static int verify_generic_valid_fallbacks(void)
{
    const std::vector<uint8_t> regular = build_input_artifact(65U, 2U);
    CHECK(!regular.empty(), "build fallback artifact");
    CHECK(expect_generic_fallback(regular, 2U, duplicate_element,
                                  "duplicate element remains prepare-valid") ==
              0,
          "duplicate element fallback");
    CHECK(expect_generic_fallback(regular, 0U, change_copy_id,
                                  "copy id remains prepare-valid") == 0,
          "copy id fallback");
    CHECK(expect_generic_fallback(regular, 0U, change_tick,
                                  "changed tick remains prepare-valid") == 0,
          "tick fallback");
    CHECK(expect_generic_fallback(regular, 2U, change_axon,
                                  "changed axon remains prepare-valid") == 0,
          "axon fallback");
    CHECK(expect_generic_fallback(regular, 0U, change_dtype,
                                  "changed dtype remains prepare-valid") == 0,
          "dtype fallback");
    CHECK(expect_generic_fallback(regular, 0U, change_valid_route,
                                  "changed route remains prepare-valid") == 0,
          "route fallback");
    CHECK(expect_generic_fallback(regular, 1U, change_target_lcn,
                                  "changed input LCN remains prepare-valid") ==
              0,
          "input LCN fallback");

    rvrt_paicore_runner_t runner = {};
    const std::vector<uint8_t> too_many_covers =
        build_input_artifact(2U, RVRT_PAICORE_RUNNER_INPUT_COVER_MAX + 1U);
    CHECK(prepare_runner(too_many_covers, &runner) == 0,
          "more than eight covers remains prepare-valid");
    CHECK(!runner.has_fast_input_layout, "more than eight covers falls back");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release excessive-cover runner");
    return 0;
}

static int verify_original_sample_failure_phase(void)
{
    std::vector<uint8_t> asset = build_input_artifact(2U, 1U);
    CHECK(!asset.empty(), "build invalid-route asset");
    fbs::InputEntry *entry = mutable_input_entry(asset, 0U);
    CHECK(entry != nullptr, "find invalid-route entry");
    auto *offset = const_cast<fbs::CoreOffset *>(entry->core_offset());
    CHECK(offset != nullptr && offset->mutate_x(-32), "make route unencodable");

    rvrt_paicore_runner_t runner = {};
    CHECK(prepare_runner(asset, &runner) == 0,
          "generic route error remains prepare-valid");
    CHECK(!runner.has_fast_input_layout, "invalid route cannot use fast path");
    std::vector<uint8_t> input(runner.input_view.element_count, 0U);
    std::array<rvrt_frame_t, 8U> frames = {};
    rvrt_input_cursor_t cursor;
    rvrt_input_cursor_init(&cursor, 0U);
    uint32_t count = 0U;
    CHECK(rvrt_encode_input_chunk(&runner.input_view, &cursor, input.data(),
                                  input.size(), frames.data(), frames.size(),
                                  &count) == RVRT_CODEC_STATUS_DONE,
          "zero payload retains generic route-skip behavior");
    CHECK(count == 0U, "zero payload emits no frame");
    input[0] = 1U;
    rvrt_input_cursor_init(&cursor, 0U);
    CHECK(rvrt_encode_input_chunk(&runner.input_view, &cursor, input.data(),
                                  input.size(), frames.data(), frames.size(),
                                  &count) == RVRT_CODEC_STATUS_BAD_VALUE,
          "invalid route still fails during generic sample encoding");
    CHECK(count == 0U, "invalid route emits no partial frame");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release invalid-route runner");
    return 0;
}

static int verify_late_schedule_validation(void)
{
    std::vector<uint8_t> invalid_lcn = build_input_artifact(65U, 2U);
    fbs::InputEntry *first = mutable_input_entry(invalid_lcn, 0U);
    fbs::InputEntry *last =
        mutable_input_entry(invalid_lcn, static_cast<uint32_t>(65U * 2U - 1U));
    CHECK((first != nullptr) && first->mutate_copy_id(1U),
          "make first entry noncanonical");
    CHECK((last != nullptr) && last->mutate_target_lcn(8U),
          "make late LCN invalid");
    rvrt_paicore_runner_t runner = {};
    CHECK(prepare_runner(invalid_lcn, &runner) != 0,
          "late invalid LCN still fails prepare after fallback");
    CHECK(runner.state == RVRT_PAICORE_RUNNER_EMPTY,
          "failed prepare clears runner");

    std::vector<uint8_t> unsupported_t = build_input_artifact(65U, 2U);
    first = mutable_input_entry(unsupported_t, 0U);
    last = mutable_input_entry(unsupported_t,
                               static_cast<uint32_t>(65U * 2U - 1U));
    CHECK((first != nullptr) && first->mutate_copy_id(1U),
          "make unsupported-T mapping noncanonical");
    CHECK((last != nullptr) && last->mutate_target_lcn(7U),
          "make late timestep window unsupported");
    const rvrt_paicore_runner_prepare_config_t config = {
        .artifact_data = unsupported_t.data(),
        .artifact_size = unsupported_t.size(),
        .voltage_state = nullptr,
        .voltage_state_capacity = 0U,
        .timeout_ms = 1U,
        .rx_policy = RVRT_PAICORE_RUNNER_RX_SPARSE,
    };
    CHECK(rvrt_paicore_runner_prepare(&runner, &config) ==
              RVRT_SESSION_SCHEDULE_UNSUPPORTED,
          "late unsupported timestep retains prepare status");
    CHECK(runner.state == RVRT_PAICORE_RUNNER_EMPTY,
          "unsupported schedule clears runner");
    return 0;
}

static int verify_cache_lifecycle(void)
{
    const std::vector<uint8_t> asset = build_input_artifact(65U, 2U);
    rvrt_paicore_runner_t runner = {};
    CHECK(prepare_runner(asset, &runner) == 0, "prepare lifecycle runner");
    CHECK(runner.has_fast_input_layout && (runner.fast_input_cover_count == 2U),
          "lifecycle runner has cache");
    std::array<rvrt_frame_t, 8U> frames = {};
    const rvrt_paicore_runner_attach_config_t attach = {
        .frame_buffer = frames.data(),
        .frame_capacity = frames.size(),
        .coverage_bitmap = nullptr,
        .coverage_word_capacity = 0U,
    };
    CHECK(rvrt_paicore_runner_attach(&runner, &attach) == RVRT_SESSION_OK,
          "attach lifecycle runner");
    CHECK(rvrt_paicore_runner_detach(&runner) == RVRT_SESSION_OK,
          "detach lifecycle runner");
    CHECK(runner.has_fast_input_layout && (runner.fast_input_cover_count == 2U),
          "detach preserves prepared cache");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release lifecycle runner");
    CHECK(!runner.has_fast_input_layout &&
              (runner.fast_input_cover_count == 0U),
          "release clears cache");
    return 0;
}

static int verify_chunk_boundaries(void)
{
    const std::vector<uint8_t> asset = build_input_artifact(4U, 1U);
    rvrt_paicore_runner_t runner = {};
    CHECK(prepare_runner(asset, &runner) == 0, "prepare chunk-boundary runner");
    CHECK(runner.has_fast_input_layout, "chunk-boundary mapping is canonical");
    std::array<rvrt_frame_t, 2U> generic = {};
    std::array<rvrt_frame_t, 2U> cached = {};
    rvrt_input_cursor_t generic_cursor;
    rvrt_input_cursor_t cached_cursor;
    uint32_t generic_count = 0U;
    uint32_t cached_count = 0U;

    const std::array<uint8_t, 4U> zero_tail = {1U, 1U, 0U, 0U};
    rvrt_input_cursor_init(&generic_cursor, 7U);
    rvrt_input_cursor_init(&cached_cursor, 7U);
    CHECK(rvrt_encode_input_chunk(&runner.input_view, &generic_cursor,
                                  zero_tail.data(), zero_tail.size(),
                                  generic.data(), generic.size(),
                                  &generic_count) == RVRT_CODEC_STATUS_DONE,
          "generic full buffer with zero tail is done");
    CHECK(rvrt_encode_input_canonical_chunk(
              runner.fast_input_prototypes, runner.fast_input_cover_count,
              runner.input_view.element_count, runner.input_view.bit_width,
              &cached_cursor, zero_tail.data(), zero_tail.size(), cached.data(),
              cached.size(), &cached_count) == RVRT_CODEC_STATUS_DONE,
          "cached full buffer with zero tail is done");
    CHECK(
        (generic_count == 2U) && (cached_count == 2U) &&
            (generic_cursor.entry_index == 4U) &&
            (cached_cursor.entry_index == 4U) &&
            (std::memcmp(generic.data(), cached.data(), sizeof(generic)) == 0),
        "zero-tail boundary remains bit exact");

    const std::array<uint8_t, 4U> late_nonzero = {1U, 1U, 0U, 1U};
    rvrt_input_cursor_init(&generic_cursor, 7U);
    rvrt_input_cursor_init(&cached_cursor, 7U);
    CHECK(rvrt_encode_input_chunk(
              &runner.input_view, &generic_cursor, late_nonzero.data(),
              late_nonzero.size(), generic.data(), generic.size(),
              &generic_count) == RVRT_CODEC_STATUS_BUFFER_FULL,
          "generic late nonzero reports buffer full");
    CHECK(rvrt_encode_input_canonical_chunk(
              runner.fast_input_prototypes, runner.fast_input_cover_count,
              runner.input_view.element_count, runner.input_view.bit_width,
              &cached_cursor, late_nonzero.data(), late_nonzero.size(),
              cached.data(), cached.size(),
              &cached_count) == RVRT_CODEC_STATUS_BUFFER_FULL,
          "cached late nonzero reports buffer full");
    CHECK((generic_count == 2U) && (cached_count == 2U) &&
              (generic_cursor.entry_index == 3U) &&
              (cached_cursor.entry_index == 3U),
          "buffer-full cursor remains on unsent entry");

    const std::array<uint8_t, 4U> short_input = {1U, 1U, 1U, 1U};
    rvrt_input_cursor_init(&generic_cursor, 0U);
    rvrt_input_cursor_init(&cached_cursor, 0U);
    CHECK(rvrt_encode_input_chunk(&runner.input_view, &generic_cursor,
                                  short_input.data(), 2U, generic.data(),
                                  generic.size(), &generic_count) ==
              RVRT_CODEC_STATUS_OUT_OF_RANGE,
          "generic short input fails after its valid prefix");
    CHECK(rvrt_encode_input_canonical_chunk(
              runner.fast_input_prototypes, runner.fast_input_cover_count,
              runner.input_view.element_count, runner.input_view.bit_width,
              &cached_cursor, short_input.data(), 2U, cached.data(),
              cached.size(), &cached_count) == RVRT_CODEC_STATUS_OUT_OF_RANGE,
          "cached short input fails after its valid prefix");
    CHECK(
        (generic_count == 2U) && (cached_count == 2U) &&
            (generic_cursor.entry_index == 2U) &&
            (cached_cursor.entry_index == 2U) &&
            (std::memcmp(generic.data(), cached.data(), sizeof(generic)) == 0),
        "short-input partial state remains identical");
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release chunk-boundary runner");
    return 0;
}

static int verify_session_guards_and_stats(void)
{
    const std::vector<uint8_t> asset = build_input_artifact(4U, 2U);
    rvrt_paicore_runner_t runner = {};
    CHECK(prepare_runner(asset, &runner) == 0, "prepare session runner");
    rvrt_session_t session = {};
    session.artifact = &runner.artifact;
    std::array<uint8_t, 4U> input = {1U, 0U, 2U, 0U};
    std::array<rvrt_frame_t, 2U> frames = {};

    host_mock_set_generator(nullptr, nullptr);
    CHECK(rvrt_session_send_canonical_input_timestep(
              &session, &runner.input_view, runner.fast_input_prototypes,
              runner.fast_input_cover_count, 0U, input.data(), input.size(),
              frames.data(), frames.size()) == RVRT_SESSION_OK,
          "canonical session send succeeds");
    CHECK(host_mock_sent_count() == 4U,
          "canonical session send preserves chunk submission");
#if RVRT_ENABLE_STATS
    CHECK((session.stats.input_frames == 4U) &&
              (session.stats.sent_frames == 4U),
          "canonical session send preserves frame statistics");
#endif

    host_mock_set_generator(nullptr, nullptr);
    session.rx_barrier.active = true;
    CHECK(rvrt_session_send_canonical_input_timestep(
              &session, &runner.input_view, runner.fast_input_prototypes,
              runner.fast_input_cover_count, 0U, input.data(), input.size(),
              frames.data(), frames.size()) == RVRT_SESSION_RUNTIME_ERROR,
          "active RX barrier rejects canonical input submission");
    CHECK(host_mock_sent_count() == 0U,
          "active barrier sends no canonical input frame");
    session.rx_barrier.active = false;

    host_mock_set_generator(nullptr, nullptr);
    session.faulted = true;
    CHECK(rvrt_session_send_canonical_input_timestep(
              &session, &runner.input_view, runner.fast_input_prototypes,
              runner.fast_input_cover_count, 0U, input.data(), input.size(),
              frames.data(), frames.size()) == RVRT_SESSION_FAULTED,
          "faulted session rejects canonical input submission");
    CHECK(host_mock_sent_count() == 0U,
          "faulted session sends no canonical input frame");
    session.faulted = false;

    input.fill(0U);
    session.rx_barrier.active = true;
    CHECK(rvrt_session_send_canonical_input_timestep(
              &session, &runner.input_view, runner.fast_input_prototypes,
              runner.fast_input_cover_count, 0U, input.data(), input.size(),
              frames.data(), frames.size()) == RVRT_SESSION_RUNTIME_ERROR,
          "zero-frame canonical chunk still submits through session guard");
    session.rx_barrier.active = false;
    CHECK(rvrt_paicore_runner_release(&runner) == RVRT_SESSION_OK,
          "release session runner");
    return 0;
}

int main(void)
{
    if ((verify_synthetic_mappings_bit_exact() != 0) ||
        (verify_generic_valid_fallbacks() != 0) ||
        (verify_original_sample_failure_phase() != 0) ||
        (verify_late_schedule_validation() != 0) ||
        (verify_cache_lifecycle() != 0) || (verify_chunk_boundaries() != 0) ||
        (verify_session_guards_and_stats() != 0)) {
        return 1;
    }
    std::puts("RUNTIME INPUT CACHE: PASS");
    return 0;
}
