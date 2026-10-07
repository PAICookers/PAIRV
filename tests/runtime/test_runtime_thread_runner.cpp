#include "compile_artifacts_generated.h"
#include "frame_codec_internal.h"
#include "mock_runtime_hw.h"
#include "runtime_session.h"
#include "runtime_session_internal.h"
#include "thread_runner.h"
#include "transport_thread_internal.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "flatbuffers/flatbuffers.h"

namespace fbs = paibox::backendv2::generated::fbs;

struct Fixture {
    flatbuffers::FlatBufferBuilder builder;
    alignas(8) std::array<uint8_t, 2048> storage = {};
    size_t storage_size = 0U;

    Fixture(fbs::OutputKind kind = fbs::OutputKind_DATA,
            fbs::DataType dtype = fbs::DataType_UINT8,
            uint32_t output_axon = 2U, uint32_t completion_target = 2U)
    {
        const auto zero_offset = fbs::CreateCoreOffset(builder, 0, 0, 0);
        const auto input_tick = fbs::CreateTickParams(builder, 0, 1, 0);
        const auto output_tick = fbs::CreateTickParams(builder, 0, 1, 0);
        const std::vector<int32_t> shape_size = {1};
        const auto shape = fbs::CreateShapeDirect(builder, &shape_size);

        std::vector<flatbuffers::Offset<fbs::InputEntry>> input0_entries;
        input0_entries.push_back(fbs::CreateInputEntry(
            builder, 0, zero_offset, fbs::CreateCopyCount(builder, 0, 0, 0), 0,
            1, 0, 0, fbs::DataType_UINT8));
        std::vector<flatbuffers::Offset<fbs::InputEntry>> input_alt_entries;
        input_alt_entries.push_back(fbs::CreateInputEntry(
            builder, 0, zero_offset, fbs::CreateCopyCount(builder, 0, 0, 0), 0,
            2, 0, 0, fbs::DataType_UINT8));
        const auto input_mapping = fbs::CreateInputTensorMappingDirect(
            builder, "input_alt", shape, 8, input_tick, &input_alt_entries);
        const auto input0_mapping = fbs::CreateInputTensorMappingDirect(
            builder, "input0", shape, 8, input_tick, &input0_entries);
        const std::vector<flatbuffers::Offset<fbs::InputTensorMapping>>
            input_mapping_items = {input0_mapping, input_mapping};
        const auto input_mappings =
            fbs::CreateInputTensorMappingsDirect(builder, &input_mapping_items);

        std::vector<flatbuffers::Offset<fbs::OutputEntry>> output0_entries;
        output0_entries.push_back(
            fbs::CreateOutputEntry(builder, 0, 0, 1, fbs::DataType_UINT8));
        std::vector<flatbuffers::Offset<fbs::OutputEntry>> output_alt_entries;
        output_alt_entries.push_back(
            fbs::CreateOutputEntry(builder, 0, 0, output_axon, dtype));
        const auto output_mapping = fbs::CreateOutputTensorMappingDirect(
            builder, "output_alt", shape, kind,
            kind == fbs::OutputKind_VOLTAGE
                ? 32
                : (dtype == fbs::DataType_UINT1 ? 1 : 8),
            output_tick, &output_alt_entries);
        const auto output0_mapping = fbs::CreateOutputTensorMappingDirect(
            builder, "output0", shape, fbs::OutputKind_DATA, 8, output_tick,
            &output0_entries);
        const std::vector<flatbuffers::Offset<fbs::OutputTensorMapping>>
            output_mapping_items = {output0_mapping, output_mapping};
        const auto output_mappings = fbs::CreateOutputTensorMappingsDirect(
            builder, 0, &output_mapping_items);

        const auto runtime0 = fbs::CreateRuntimeParams(
            builder, 2, 1, completion_target, fbs::DecodeMode_STREAM);
        const auto runtime1 =
            fbs::CreateRuntimeParams(builder, 3, 1, 3, fbs::DecodeMode_STREAM);
        const auto root0 = fbs::CreateCoreOffset(builder, 0, 0, 1);
        const auto root1 = fbs::CreateCoreOffset(builder, 0, 0, 2);
        const std::vector<flatbuffers::Offset<fbs::CoreTick>> empty_ticks;
        const auto ticks = builder.CreateVector(empty_ticks);
        const auto thread0 =
            fbs::CreateThreadIOMapping(builder, 0, root0, runtime0,
                                       input_mappings, output_mappings, ticks);
        const auto thread1 =
            fbs::CreateThreadIOMapping(builder, 1, root1, runtime1,
                                       input_mappings, output_mappings, ticks);
        const auto threads =
            builder.CreateVector<flatbuffers::Offset<fbs::ThreadIOMapping>>(
                {thread0, thread1});
        const auto io_mapping = fbs::CreateIOMapping(builder, threads);
        const auto config_words = builder.CreateVector<uint32_t>({0, 0});
        const auto config = fbs::CreateConfigFrames(builder, config_words,
                                                    fbs::WordOrder_HIGH_FIRST);
        const auto artifact =
            fbs::CreateCompileArtifacts(builder, 1, io_mapping, config);
        fbs::FinishCompileArtifactsBuffer(builder, artifact);
        storage_size = builder.GetSize();
        std::memcpy(storage.data(), builder.GetBufferPointer(), storage_size);
    }

    const uint8_t *data() const { return storage.data(); }
    size_t size() const { return storage_size; }
};

static bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
    }
    return condition;
}

static bool queue_completions(uint32_t count)
{
    std::vector<rvrt_frame_t> frames(count, {0xE0000000U, 0U});
    mock_runtime_queue_rx(frames.data(), count);
    return true;
}

/* Run the same two-timestep sample through cached and generic layouts. This
 * verifies dispatch and lane state without measuring the mock clock. */
static bool verify_fast_decode(fbs::OutputKind kind, fbs::DataType dtype)
{
    const Fixture fixture(kind, dtype, 0U, 3U);
    mock_runtime_reset();
    rvrt_frame_t workspace[2] = {};
    rvrt_runtime_session_t session = {};
    rvrt_thread_runner_t runner = {};
    rvrt_voltage_decode_state_t state[2] = {};
    const rvrt_runtime_session_open_config_t session_config = {
        fixture.data(), fixture.size(), workspace, 2U};
    const rvrt_thread_runner_open_config_t runner_config = {
        &session, 0U, 0U, 1U, state, 2U, 10U};
    if ((rvrt_runtime_session_open(&session, &session_config) !=
         RVRT_RUNTIME_OK) ||
        (rvrt_runtime_session_configure(&session) != RVRT_RUNTIME_OK) ||
        (rvrt_thread_runner_open(&runner, &runner_config) != RVRT_RUNTIME_OK)) {
        return false;
    }
    const bool voltage = kind == fbs::OutputKind_VOLTAGE;
    bool ok =
        voltage ? runner.has_fast_voltage_layout : runner.has_fast_data_layout;
    const uint8_t input[2] = {1U, 1U};
    alignas(int32_t) uint8_t fast_output[8] = {};
    alignas(int32_t) uint8_t generic_output[8] = {};
    rvrt_voltage_decode_state_t fast_state[2] = {};
    const uint32_t values[2] = {0x12345678U, 0xFEDCBA98U};
    std::vector<rvrt_frame_t> frames = {{0xE0000000U, 0U}};
    for (uint32_t timestep = 0U; timestep < 2U; ++timestep) {
        const uint32_t lanes = voltage ? 4U : 1U;
        for (uint32_t i = 0U; i < lanes; ++i) {
            const uint32_t lane = lanes - 1U - i;
            frames.push_back(
                {voltage ? 0xA0000000U : 0x80000000U,
                 (timestep << 17U) | (lane << 11U) |
                     (voltage ? ((values[timestep] >> (lane * 8U)) & 0xFFU)
                              : 1U)});
        }
        frames.push_back({0xE0000000U, 0U});
    }
    frames.push_back({0xE0000000U, 0U}); // One final tail-drain barrier.
    const size_t output_size = voltage ? sizeof(fast_output) : 2U;
    rv_counter_t sync_cycles[3] = {};
    rvrt_thread_runner_sample_timing_t timing = {0U, sync_cycles, 3U, 0U};
    mock_runtime_queue_rx(frames.data(), static_cast<uint32_t>(frames.size()));
    const rvrt_runtime_status_t fast_status =
        rvrt_thread_runner_run_sample_profiled(&runner, input, sizeof(input),
                                               0U, fast_output, output_size, 0U,
                                               &timing);
    ok = ok && (fast_status == RVRT_RUNTIME_OK);
#if RVRT_ENABLE_STATS
    ok = ok && (timing.sync_round_trip_count == 3U) && (sync_cycles[2] != 0U);
#else
    ok = ok && (timing.sync_round_trip_count == 0U);
#endif
    std::memcpy(fast_state, state, sizeof(state));
    runner.has_fast_data_layout = false;
    runner.has_fast_voltage_layout = false;
    mock_runtime_queue_rx(frames.data(), static_cast<uint32_t>(frames.size()));
    const rvrt_runtime_status_t generic_status = rvrt_thread_runner_run_sample(
        &runner, input, sizeof(input), 0U, generic_output, output_size, 0U);
    ok = ok && (generic_status == RVRT_RUNTIME_OK) &&
         (std::memcmp(fast_output, generic_output, output_size) == 0) &&
         (std::memcmp(state, fast_state, sizeof(state)) == 0);
    if (voltage) {
        ok = ok && (std::memcmp(fast_output, values, output_size) == 0);
    } else {
        ok = ok && (fast_output[0] == 1U) && (fast_output[1] == 1U);
    }
    if (!ok) {
        std::fprintf(stderr, "decode statuses=%d/%d output=%u,%u/%u,%u\n",
                     fast_status, generic_status, fast_output[0],
                     fast_output[1], generic_output[0], generic_output[1]);
    }
    ok = (rvrt_thread_runner_close(&runner) == RVRT_RUNTIME_OK) && ok;
    const Fixture noncanonical(kind, dtype, 2U);
    rvrt_artifact_view_t artifact = {};
    rvrt_artifact_output_mapping_view_t view = {};
    bool fast_data = true;
    bool fast_voltage = true;
    ok = (rvrt_artifact_read(noncanonical.data(), noncanonical.size(),
                             &artifact) == RVRT_ARTIFACT_OK) &&
         ok;
    ok = (rvrt_artifact_get_output_mapping_view(&artifact, 0U, 1U, &view) ==
          RVRT_ARTIFACT_OK) &&
         ok;
    ok = (rvrt_output_fast_layout(&view, &fast_data, &fast_voltage) ==
          RVRT_CODEC_STATUS_OK) &&
         !fast_data && !fast_voltage && ok;
    ok = (rvrt_runtime_session_close(&session) == RVRT_RUNTIME_OK) && ok;
    return ok;
}

static rvrt_frame_t work_frame_at(uint32_t timestamp)
{
    return {0x80000000U | (((timestamp >> 7U) & 1U) << 28U), (timestamp & 0x7FU)
                                                                 << 17U};
}

enum class FaultKind { Timeout, Overflow, Hardware, Handler };

static bool run_fault_case(const Fixture &fixture, FaultKind kind)
{
    mock_runtime_reset();
    rvrt_frame_t rx_frames[2] = {};
    rvrt_runtime_session_t session = {};
    const rvrt_runtime_session_open_config_t session_config = {
        fixture.data(), fixture.size(), rx_frames,
        kind == FaultKind::Overflow ? 1U : 2U};
    if ((rvrt_runtime_session_open(&session, &session_config) !=
         RVRT_RUNTIME_OK) ||
        (rvrt_runtime_session_configure(&session) != RVRT_RUNTIME_OK)) {
        return false;
    }
    rvrt_thread_runner_t runner = {};
    const rvrt_thread_runner_open_config_t runner_config = {
        &session, 0U, 0U, 0U, nullptr, 0U, 1U};
    if (rvrt_thread_runner_open(&runner, &runner_config) != RVRT_RUNTIME_OK) {
        return false;
    }

    if (kind == FaultKind::Overflow) {
        (void)rvrt_thread_runner_close(&runner);
        const rvrt_frame_t frames[] = {
            {0x80000000U, 0U}, {0x80000000U, 0U}, {0xE0000000U, 0U}};
        mock_runtime_queue_rx(frames, 3U);
        const rvrt_frame_t *received = nullptr;
        uint32_t received_count = 0U;
        rvrt_runtime_sync_mode_t sync_mode = RVRT_RUNTIME_SYNC_MODE_UNSET;
        const rvrt_runtime_status_t status =
            rvrt_transport_sync_wait_payload_for_thread(
                &session.transport, 0U, &sync_mode, 1U, 1U, &received,
                &received_count);
        rvrt_runtime_session_note_status(&session, status);
        return (status == RVRT_RUNTIME_OVERFLOW) && session.faulted &&
               (rvrt_runtime_session_close(&session) == RVRT_RUNTIME_OK);
    } else if (kind == FaultKind::Hardware) {
        mock_runtime_set_fifo_read_error(true);
        queue_completions(1U);
    } else if (kind == FaultKind::Handler) {
        const rvrt_frame_t frames[] = {
            {0xE0000000U, 0U}, work_frame_at(2U), {0xE0000000U, 0U}};
        const uint32_t segments[] = {1U, 2U};
        mock_runtime_queue_rx_segments(frames, 3U, segments, 2U);
    }

    uint8_t input[2] = {1U, 1U};
    uint8_t output[2] = {};
    const rvrt_runtime_status_t status = rvrt_thread_runner_run_sample(
        &runner, input, sizeof(input), 0U, output, sizeof(output), 0U);
    const rvrt_runtime_status_t expected =
        kind == FaultKind::Timeout ? RVRT_RUNTIME_TIMEOUT
                                   : (kind == FaultKind::Overflow
                                          ? RVRT_RUNTIME_OVERFLOW
                                          : (kind == FaultKind::Hardware
                                                 ? RVRT_RUNTIME_HARDWARE_ERROR
                                                 : RVRT_RUNTIME_RUNTIME_ERROR));
    const bool sticky = (status == expected) &&
                        (rvrt_thread_runner_run_sample(
                             &runner, input, sizeof(input), 0U, output,
                             sizeof(output), 0U) == RVRT_RUNTIME_FAULTED);
    const bool closed =
        (rvrt_thread_runner_close(&runner) == RVRT_RUNTIME_OK) &&
        (rvrt_runtime_session_close(&session) == RVRT_RUNTIME_OK);
    return sticky && closed;
}

int main()
{
    mock_runtime_reset();
    Fixture fixture;
    rvrt_frame_t rx_frames[16] = {};
    rvrt_runtime_session_t session = {};
    const rvrt_runtime_session_open_config_t session_config = {
        fixture.data(), fixture.size(), rx_frames, 16U};
    rvrt_runtime_session_t boundary_session = {};
    if (!expect(rvrt_runtime_session_open(nullptr, &session_config) ==
                    RVRT_RUNTIME_RUNTIME_ERROR,
                "NULL runtime session was accepted") ||
        !expect(rvrt_runtime_session_open(&boundary_session, nullptr) ==
                    RVRT_RUNTIME_RUNTIME_ERROR,
                "NULL session config was accepted")) {
        return 1;
    }
    const rvrt_runtime_status_t open_status =
        rvrt_runtime_session_open(&session, &session_config);
    if (!expect(open_status == RVRT_RUNTIME_OK,
                rvrt_runtime_session_status_string(open_status))) {
        return 1;
    }
    rvrt_thread_runner_t unconfigured_runner = {};
    const rvrt_thread_runner_open_config_t unconfigured_config = {
        &session, 0U, 0U, 0U, nullptr, 0U, 10U};
    if (!expect(rvrt_thread_runner_open(&unconfigured_runner,
                                        &unconfigured_config) ==
                    RVRT_RUNTIME_RUNTIME_ERROR,
                "runner opened before session configure") ||
        !expect(rvrt_runtime_session_configure(&session) == RVRT_RUNTIME_OK,
                "runtime session configure failed") ||
        !expect(rvrt_runtime_session_configure(&session) == RVRT_RUNTIME_OK,
                "runtime session configure was not idempotent")) {
        return 1;
    }

    rvrt_thread_runner_t first = {};
    rvrt_thread_runner_t second = {};
    const rvrt_thread_runner_open_config_t first_config = {&session, 0U, 1U, 1U,
                                                           nullptr,  0U, 10U};
    const rvrt_thread_runner_open_config_t second_config = {
        &session, 1U, 1U, 1U, nullptr, 0U, 10U};
    if (!expect(rvrt_thread_runner_open(&first, &first_config) ==
                    RVRT_RUNTIME_OK,
                "first runner open failed") ||
        !expect(rvrt_thread_runner_open(&second, &second_config) ==
                    RVRT_RUNTIME_OK,
                "second runner open failed")) {
        return 1;
    }

    rvrt_artifact_input_entry_t first_input_entry = {};
    rvrt_artifact_input_entry_t second_input_entry = {};
    rvrt_artifact_output_entry_t first_output_entry = {};
    rvrt_artifact_output_entry_t second_output_entry = {};
    bool first_output_found = false;
    bool second_output_found = false;
    if (!expect(rvrt_artifact_input_mapping_entry(&first.input_view, 0U,
                                                  &first_input_entry) ==
                    RVRT_ARTIFACT_OK,
                "first input mapping lookup failed") ||
        !expect(rvrt_artifact_input_mapping_entry(&second.input_view, 0U,
                                                  &second_input_entry) ==
                    RVRT_ARTIFACT_OK,
                "second input mapping lookup failed") ||
        !expect(rvrt_artifact_output_mapping_find(
                    &first.output_view, 2U, &first_output_entry,
                    &first_output_found) == RVRT_ARTIFACT_OK &&
                    first_output_found,
                "first output mapping lookup failed") ||
        !expect(rvrt_artifact_output_mapping_find(
                    &second.output_view, 2U, &second_output_entry,
                    &second_output_found) == RVRT_ARTIFACT_OK &&
                    second_output_found,
                "second output mapping lookup failed") ||
        !expect(first_input_entry.addr_axon == 2U,
                "input mapping index was not selected") ||
        !expect(first_output_entry.axon_bit_idx == 2U,
                "output mapping index was not selected") ||
        !expect(second_input_entry.addr_axon == 2U,
                "second runner input mapping changed") ||
        !expect(second_output_entry.axon_bit_idx == 2U,
                "second runner output mapping changed")) {
        return 1;
    }

    uint8_t first_input[2] = {1U, 1U};
    uint8_t first_output[2] = {};
    queue_completions(3U);
    if (!expect(rvrt_thread_runner_run_sample(
                    &first, first_input, sizeof(first_input), 0U, first_output,
                    sizeof(first_output), 0U) == RVRT_RUNTIME_OK,
                "first runner sample failed")) {
        return 1;
    }

    uint8_t second_input[3] = {1U, 1U, 1U};
    uint8_t second_output[3] = {};
    queue_completions(4U);
    if (!expect(rvrt_thread_runner_run_sample(
                    &second, second_input, sizeof(second_input), 0U,
                    second_output, sizeof(second_output),
                    0U) == RVRT_RUNTIME_OK,
                "second runner sample failed")) {
        return 1;
    }

    rvrt_frame_t expected_init = {};
    if (!expect(rvrt_build_init_frame(&session.artifact, 1U, &expected_init) ==
                    RVRT_CODEC_STATUS_OK,
                "second init frame build failed")) {
        return 1;
    }

    const rvrt_frame_t *sent = mock_runtime_sent_frames();
    const uint32_t sent_count = mock_runtime_sent_count();
    bool found_second_init = false;
    for (uint32_t i = 0U; i < sent_count; ++i) {
        if ((sent[i].high == expected_init.high) &&
            (sent[i].low == expected_init.low)) {
            found_second_init = true;
            break;
        }
    }
    if (!expect(found_second_init, "thread-rooted INIT was not routed")) {
        return 1;
    }

    rvrt_runtime_session_t second_session = {};
    if (!expect(rvrt_runtime_session_open(&second_session, &session_config) ==
                    RVRT_RUNTIME_BUSY,
                "second physical session was not rejected")) {
        return 1;
    }
    if (!expect(rvrt_runtime_session_close(&session) == RVRT_RUNTIME_BUSY,
                "session close ignored active runners") ||
        !expect(rvrt_thread_runner_close(&first) == RVRT_RUNTIME_OK,
                "first runner close failed") ||
        !expect(rvrt_thread_runner_close(&second) == RVRT_RUNTIME_OK,
                "second runner close failed") ||
        !expect(rvrt_runtime_session_close(&session) == RVRT_RUNTIME_OK,
                "runtime session close failed")) {
        return 1;
    }
    if (!expect(run_fault_case(fixture, FaultKind::Timeout),
                "timeout fault case failed") ||
        !expect(run_fault_case(fixture, FaultKind::Overflow),
                "overflow fault case failed") ||
        !expect(run_fault_case(fixture, FaultKind::Hardware),
                "hardware fault case failed") ||
        !expect(run_fault_case(fixture, FaultKind::Handler),
                "handler fault case failed")) {
        return 1;
    }
    if (!expect(verify_fast_decode(fbs::OutputKind_DATA, fbs::DataType_UINT1),
                "fast DATA differs from generic decoder") ||
        !expect(
            verify_fast_decode(fbs::OutputKind_VOLTAGE, fbs::DataType_UINT8),
            "fast VOLTAGE differs from generic decoder")) {
        return 1;
    }
    std::puts("thread runner tests passed");
    return 0;
}
