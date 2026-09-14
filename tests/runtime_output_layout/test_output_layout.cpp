#include "generated/compile_artifacts_generated.h"
#include "paicore_runner.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" void mock_set_sync_frames(const rvrt_frame_t *frames,
                                     uint32_t first_count,
                                     uint32_t second_count);

namespace fbs = paibox::backendv2::generated::fbs;

struct Layout {
    const char *name;
    bool voltage;
    uint32_t base;
    uint32_t lcn;
    bool hole;
    bool duplicate;
};

// The ninth VOLTAGE element starts a second eight-element lane group.
static const uint32_t voltage_axons[] = {0, 1, 2, 3, 4, 5, 6, 7, 32};
static const int32_t voltage_values[2][9] = {
    {0, 1, -1, 0x12345678, -123456, 128, 255, 65536, INT32_MIN},
    {-1, 0, 1, -123456, 0x12345678, 255, 128, INT32_MIN, 65536},
};
static const uint8_t data_values[2][4] = {{1, 0, 1, 0}, {0, 1, 0, 1}};
static const uint8_t duplicate_expected[2][4] = {{0, 0, 1, 0}, {1, 0, 0, 1}};

static uint32_t axon_at(const Layout &layout, uint32_t element)
{
    uint32_t offset = layout.voltage ? voltage_axons[element] : element;
    if (layout.hole && element != 0U) {
        offset += layout.voltage ? 32U : 1U;
    }
    return layout.base + offset;
}

static std::vector<uint8_t> build_artifact(const Layout &layout)
{
    flatbuffers::FlatBufferBuilder b;
    const uint32_t elements = layout.voltage ? 9U : 4U;
    const auto core = fbs::CreateCoreOffset(b, 0, 0, 0);
    const auto copy = fbs::CreateCopyCount(b, 0, 0, 0);
    const auto tick = fbs::CreateTickParams(b, 0, 1, 0);
    const std::vector<int32_t> input_dims = {1};
    const std::vector<int32_t> output_dims = {static_cast<int32_t>(elements)};
    const auto input_shape = fbs::CreateShapeDirect(b, &input_dims);
    const auto output_shape = fbs::CreateShapeDirect(b, &output_dims);
    const std::vector<flatbuffers::Offset<fbs::InputEntry>> input_entries = {
        fbs::CreateInputEntry(b, 0, core, copy, 0, 0, 0, 0,
                              fbs::DataType_UINT1)};
    const auto input = fbs::CreateInputTensorMappingDirect(
        b, "input", input_shape, 1, tick, &input_entries);
    const std::vector<flatbuffers::Offset<fbs::InputTensorMapping>> inputs = {
        input};
    const auto input_mappings =
        fbs::CreateInputTensorMappingsDirect(b, &inputs);
    std::vector<flatbuffers::Offset<fbs::OutputEntry>> output_entries;
    for (uint32_t element = 0U; element < elements; ++element) {
        const uint32_t mapped_element =
            layout.duplicate && element == 1U ? 0U : element;
        output_entries.push_back(fbs::CreateOutputEntry(
            b, mapped_element, 0, axon_at(layout, element),
            fbs::DataType_UINT1));
    }
    const auto output = fbs::CreateOutputTensorMappingDirect(
        b, "output", output_shape,
        layout.voltage ? fbs::OutputKind_VOLTAGE : fbs::OutputKind_DATA,
        layout.voltage ? 32 : 1, tick, &output_entries);
    const std::vector<flatbuffers::Offset<fbs::OutputTensorMapping>> outputs = {
        output};
    const auto output_mappings =
        fbs::CreateOutputTensorMappingsDirect(b, layout.lcn, &outputs);
    const auto runtime =
        fbs::CreateRuntimeParams(b, 2, 1, 2, fbs::DecodeMode_STREAM);
    const std::vector<flatbuffers::Offset<fbs::CoreTick>> core_ticks;
    const auto thread = fbs::CreateThreadIOMappingDirect(
        b, 0, core, runtime, input_mappings, output_mappings, &core_ticks);
    const std::vector<flatbuffers::Offset<fbs::ThreadIOMapping>> threads = {
        thread};
    const auto io = fbs::CreateIOMappingDirect(b, &threads);
    const std::vector<uint32_t> config_words = {0U, 0U};
    const auto config = fbs::CreateConfigFramesDirect(
        b, &config_words, fbs::WordOrder_HIGH_FIRST);
    const auto root = fbs::CreateCompileArtifacts(b, 1, io, config);
    fbs::FinishCompileArtifactsBuffer(b, root);
    return {b.GetBufferPointer(), b.GetBufferPointer() + b.GetSize()};
}

// Encode the documented wire fields independently of the runtime codec.
static rvrt_frame_t output_frame(const Layout &layout, uint32_t timestep,
                                 uint32_t axon, uint32_t payload)
{
    const uint32_t timestamp = (timestep << layout.lcn) | (axon >> 9U);
    return {(layout.voltage ? 0xA0000000U : 0x80000000U) |
                (((timestamp >> 7U) & 1U) << 28U),
            ((timestamp & 0x7FU) << 17U) | ((axon & 0x1FFU) << 8U) | payload};
}

static bool verify_layout(const Layout &layout)
{
    const auto artifact = build_artifact(layout);
    rvrt_artifact_t parsed = {};
    if (rvrt_artifact_read(artifact.data(), artifact.size(), &parsed) !=
        RVRT_ARTIFACT_OK) {
        std::fprintf(stderr, "%s: invalid test artifact\n", layout.name);
        return false;
    }
    const uint32_t elements = layout.voltage ? 9U : 4U;
    const size_t row_bytes = elements * (layout.voltage ? sizeof(int32_t) : 1U);
    const size_t stride = row_bytes + 8U;
    std::vector<rvrt_frame_t> replies;
    uint32_t first_count = 0U;
    for (uint32_t timestep = 0U; timestep < 2U; ++timestep) {
        for (uint32_t element = 0U; element < elements; ++element) {
            if (layout.voltage) {
                const uint32_t value =
                    static_cast<uint32_t>(voltage_values[timestep][element]);
                // Reverse lane order ensures assembly does not depend on
                // arrival order.
                for (int lane = 3; lane >= 0; --lane) {
                    replies.push_back(
                        output_frame(layout, timestep,
                                     axon_at(layout, element) +
                                         static_cast<uint32_t>(lane) * 8U,
                                     (value >> (lane * 8U)) & 0xFFU));
                }
            } else {
                replies.push_back(output_frame(layout, timestep,
                                               axon_at(layout, element),
                                               data_values[timestep][element]));
            }
        }
        // Unmapped addresses follow valid values, so an accidental write is
        // visible.
        if (layout.base != 0U) {
            replies.push_back(
                output_frame(layout, timestep, layout.base - 1U, 1U));
        }
        const uint32_t beyond =
            axon_at(layout, elements - 1U) + (layout.voltage ? 32U : 1U);
        replies.push_back(output_frame(layout, timestep, beyond, 1U));
        replies.push_back({0xE0000000U, 0U});
        if (timestep == 0U) {
            first_count = static_cast<uint32_t>(replies.size());
        }
    }
    mock_set_sync_frames(replies.data(), first_count,
                         static_cast<uint32_t>(replies.size()) - first_count);
    rvrt_paicore_runner_t runner = {};
    rvrt_frame_t frames[4] = {};
    rvrt_voltage_decode_state_t voltage_state[18] = {};
    const rvrt_paicore_runner_deploy_config_t config = {
        artifact.data(),
        artifact.size(),
        frames,
        4U,
        layout.voltage ? voltage_state : nullptr,
        layout.voltage ? 18U : 0U,
        100U};
    bool passed = true;
    // Reuse the same caller-owned descriptor after release; test real output
    // twice.
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        const auto deploy_status = rvrt_paicore_runner_deploy(&runner, &config);
        alignas(int32_t) std::array<uint8_t, 96> output;
        output.fill(0xA5U);
        const uint8_t input[2] = {0U, 0U};
        const auto status = rvrt_paicore_runner_run_sample(
            &runner, input, sizeof(input), 0U, output.data(), output.size(),
            stride);
        if (deploy_status != RVRT_SESSION_OK || status != RVRT_SESSION_OK) {
            std::fprintf(stderr, "%s: deploy=%d sample=%d on repeat %u\n",
                         layout.name, deploy_status, status, repeat);
            passed = false;
        } else {
            std::array<uint8_t, 96> expected;
            expected.fill(0xA5U);
            for (unsigned t = 0; t < 2; ++t) {
                const void *values =
                    layout.voltage
                        ? static_cast<const void *>(voltage_values[t])
                        : static_cast<const void *>(layout.duplicate
                                                        ? duplicate_expected[t]
                                                        : data_values[t]);
                std::memcpy(expected.data() + t * stride, values, row_bytes);
            }
            if (output != expected) {
                std::fprintf(stderr,
                             "%s: wrong output or overwritten stride padding\n",
                             layout.name);
                passed = false;
            }
        }
        if (rvrt_paicore_runner_release(&runner) != RVRT_SESSION_OK) {
            std::fprintf(stderr, "%s: release failed\n", layout.name);
            return false;
        }
    }
    return passed;
}

int main()
{
    const Layout cases[] = {
        {"data-zero", false, 0, 0, false, false},
        {"data-nonzero", false, 32, 0, false, false},
        {"data-high-axon", false, 512, 1, false, false},
        {"data-hole", false, 32, 0, true, false},
        {"data-duplicate-element", false, 32, 0, false, true},
        {"voltage-zero", true, 0, 0, false, false},
        {"voltage-nonzero", true, 64, 0, false, false},
        {"voltage-high-axon", true, 512, 1, false, false},
        {"voltage-hole", true, 64, 0, true, false},
    };
    unsigned failures = 0;
    for (const auto &layout : cases) {
        const bool passed = verify_layout(layout);
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", layout.name);
        failures += !passed;
    }
    std::printf("%zu layouts, %u failed (stats=%d)\n",
                sizeof(cases) / sizeof(cases[0]), failures, RVRT_ENABLE_STATS);
    return failures == 0U ? 0 : 1;
}
