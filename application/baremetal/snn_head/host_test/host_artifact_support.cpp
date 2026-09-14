/*
 * host_artifact_support.cpp —— choreography 测试的 C++ 辅助能力，只在 host 用：
 *
 *  host_enum_output_axons():
 *     枚举某层 output mapping 的 (elem_idx -> axon_bit_idx)。哨兵回显需要据此构造能被
 *     decode 命中的输出帧：DATA 用 axon_bit_idx 直接命中元素；VOLTAGE 的 axon_bit_idx
 *     即该元素 lane-0 的 base，四个 lane 为 base + lane*8。
 *
 * 复用 artifact_reader 的导航路径（见 Lib/runtime/artifact_reader.cpp）：
 *   artifact.io_mapping -> threads()[i] -> runtime() / output_mappings()->items()[0]->entries()
 */
#include "artifact_reader.h"
#include "generated/compile_artifacts_generated.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace fbs = paibox::backendv2::generated::fbs;

extern "C" int host_enum_output_axons(const uint8_t *buf, unsigned size,
                                      uint32_t *elem_to_axon,
                                      uint32_t max_elems, uint32_t *out_count)
{
    if ((elem_to_axon == nullptr) || (out_count == nullptr)) {
        return 1;
    }
    *out_count = 0U;

    rvrt_artifact_t artifact;
    if (rvrt_artifact_read(buf, size, &artifact) != RVRT_ARTIFACT_OK) {
        return 2;
    }
    const auto *io = static_cast<const fbs::IOMapping *>(artifact.io_mapping);
    if ((io == nullptr) || (io->threads() == nullptr) ||
        (io->threads()->size() == 0U)) {
        return 3;
    }
    const auto *thread = io->threads()->Get(0);
    const auto *mappings =
        (thread != nullptr) ? thread->output_mappings() : nullptr;
    if ((mappings == nullptr) || (mappings->items() == nullptr) ||
        (mappings->items()->size() == 0U)) {
        return 4;
    }
    const auto *mapping = mappings->items()->Get(0);
    if ((mapping == nullptr) || (mapping->entries() == nullptr)) {
        return 5;
    }

    uint32_t count = 0U;
    for (const auto *entry : *mapping->entries()) {
        if (entry == nullptr) {
            continue;
        }
        const uint32_t elem = entry->elem_idx();
        if (elem >= max_elems) {
            return 6;
        }
        elem_to_axon[elem] = entry->axon_bit_idx();
        if ((elem + 1U) > count) {
            count = elem + 1U;
        }
    }
    *out_count = count;
    return 0;
}

extern "C" int host_offset_output_axons(uint8_t *buf, unsigned size,
                                        uint32_t delta)
{
    rvrt_artifact_t artifact;
    if (rvrt_artifact_read(buf, size, &artifact) != RVRT_ARTIFACT_OK) {
        return 1;
    }
    auto *io = const_cast<fbs::IOMapping *>(
        static_cast<const fbs::IOMapping *>(artifact.io_mapping));
    if ((io == nullptr) || (io->threads() == nullptr) ||
        (io->threads()->size() == 0U)) {
        return 2;
    }
    auto *thread = const_cast<fbs::ThreadIOMapping *>(io->threads()->Get(0));
    auto *mappings =
        (thread != nullptr)
            ? const_cast<fbs::OutputTensorMappings *>(thread->output_mappings())
            : nullptr;
    if ((mappings == nullptr) || (mappings->items() == nullptr) ||
        (mappings->items()->size() == 0U)) {
        return 3;
    }
    auto *mapping =
        const_cast<fbs::OutputTensorMapping *>(mappings->items()->Get(0));
    if ((mapping == nullptr) || (mapping->entries() == nullptr)) {
        return 4;
    }
    for (auto *const_entry : *mapping->entries()) {
        auto *entry = const_cast<fbs::OutputEntry *>(const_entry);
        if ((entry == nullptr) ||
            (entry->axon_bit_idx() > UINT32_MAX - delta)) {
            return 5;
        }
        const uint32_t old_axon = entry->axon_bit_idx();
        if ((old_axon != 0U) && !entry->mutate_axon_bit_idx(old_axon + delta)) {
            return 5;
        }
    }
    return rvrt_artifact_read(buf, size, &artifact) == RVRT_ARTIFACT_OK ? 0 : 6;
}

extern "C" int host_build_data_artifact(uint8_t *buf, unsigned capacity,
                                        unsigned *size_out, uint32_t thread_id,
                                        uint32_t output_base, uint32_t layout,
                                        uint32_t pipeline_latency)
{
    if ((buf == nullptr) || (size_out == nullptr)) {
        return 1;
    }
    flatbuffers::FlatBufferBuilder builder;
    const auto core = fbs::CreateCoreOffset(builder, 0, 0, 0);
    const auto copy = fbs::CreateCopyCount(builder, 0, 0, 0);
    const auto tick = fbs::CreateTickParams(builder, 0, 1, 0);
    const std::vector<int32_t> input_dims = {1};
    const std::vector<int32_t> output_dims = {4};
    const auto input_shape = fbs::CreateShapeDirect(builder, &input_dims);
    const auto output_shape = fbs::CreateShapeDirect(builder, &output_dims);
    std::vector<flatbuffers::Offset<fbs::InputEntry>> input_entries = {
        fbs::CreateInputEntry(builder, 0, core, copy, 0, 0, 0, 0,
                              fbs::DataType_UINT1),
    };
    const auto input = fbs::CreateInputTensorMappingDirect(
        builder, "input", input_shape, 1, tick, &input_entries);
    std::vector<flatbuffers::Offset<fbs::InputTensorMapping>> inputs = {input};
    const auto input_mappings =
        fbs::CreateInputTensorMappingsDirect(builder, &inputs);

    std::vector<flatbuffers::Offset<fbs::OutputEntry>> output_entries;
    for (uint32_t elem = 0U; elem < 4U; ++elem) {
        uint32_t mapped_elem = elem;
        uint32_t mapped_axon = output_base + elem;
        if ((layout == 1U) && (elem != 0U)) {
            mapped_axon++;
        } else if ((layout == 2U) && (elem == 1U)) {
            mapped_elem = 0U;
        }
        output_entries.push_back(fbs::CreateOutputEntry(
            builder, mapped_elem, 0, mapped_axon, fbs::DataType_UINT1));
    }
    const auto output = fbs::CreateOutputTensorMappingDirect(
        builder, "output", output_shape, fbs::OutputKind_DATA, 1, tick,
        &output_entries);
    std::vector<flatbuffers::Offset<fbs::OutputTensorMapping>> outputs = {
        output};
    const auto output_mappings =
        fbs::CreateOutputTensorMappingsDirect(builder, 4, &outputs);
    const auto runtime = fbs::CreateRuntimeParams(
        builder, 1, pipeline_latency, pipeline_latency, fbs::DecodeMode_STREAM);
    const std::vector<flatbuffers::Offset<fbs::CoreTick>> core_ticks;
    const auto thread = fbs::CreateThreadIOMappingDirect(
        builder, thread_id, core, runtime, input_mappings, output_mappings,
        &core_ticks);
    const std::vector<flatbuffers::Offset<fbs::ThreadIOMapping>> threads = {
        thread};
    const auto io = fbs::CreateIOMappingDirect(builder, &threads);
    const std::vector<uint32_t> config_words = {0U, 0U};
    const auto config = fbs::CreateConfigFramesDirect(
        builder, &config_words, fbs::WordOrder_HIGH_FIRST);
    const auto root = fbs::CreateCompileArtifacts(builder, 1, io, config);
    fbs::FinishCompileArtifactsBuffer(builder, root);
    if (builder.GetSize() > capacity) {
        return 2;
    }
    std::memcpy(buf, builder.GetBufferPointer(), builder.GetSize());
    *size_out = static_cast<unsigned>(builder.GetSize());
    rvrt_artifact_t artifact;
    return rvrt_artifact_read(buf, *size_out, &artifact) == RVRT_ARTIFACT_OK
               ? 0
               : 3;
}
