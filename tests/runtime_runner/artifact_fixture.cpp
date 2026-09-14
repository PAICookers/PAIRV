#include "generated/compile_artifacts_generated.h"
#include "support.h"
#include <cstring>
#include <vector>
namespace fbs = paibox::backendv2::generated::fbs;

extern "C" int host_build_artifact(uint8_t *buf, unsigned capacity,
                                   unsigned *size_out, uint32_t thread_id,
                                   uint32_t output_base, uint32_t layout,
                                   uint32_t pipeline_latency, bool voltage,
                                   uint32_t timesteps)
{
    if ((buf == nullptr) || (size_out == nullptr)) {
        return 1;
    }
    flatbuffers::FlatBufferBuilder builder;
    const auto core = fbs::CreateCoreOffset(builder, 0, 0, 0);
    const auto copy = fbs::CreateCopyCount(builder, 0, 0, 0);
    const auto tick = fbs::CreateTickParams(builder, 0, timesteps, 0);
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
            builder, mapped_elem, 0, mapped_axon,
            voltage ? fbs::DataType_INT8 : fbs::DataType_UINT1));
    }
    const auto output = fbs::CreateOutputTensorMappingDirect(
        builder, "output", output_shape,
        voltage ? fbs::OutputKind_VOLTAGE : fbs::OutputKind_DATA,
        voltage ? 32 : 1, tick, &output_entries);
    std::vector<flatbuffers::Offset<fbs::OutputTensorMapping>> outputs = {
        output};
    const auto output_mappings =
        fbs::CreateOutputTensorMappingsDirect(builder, 4, &outputs);
    const auto runtime = fbs::CreateRuntimeParams(
        builder, timesteps, pipeline_latency, pipeline_latency + timesteps - 1U,
        fbs::DecodeMode_STREAM);
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

extern "C" int host_build_data_artifact(uint8_t *buf, unsigned capacity,
                                        unsigned *size_out, uint32_t thread_id,
                                        uint32_t output_base, uint32_t layout,
                                        uint32_t pipeline_latency)
{
    return host_build_artifact(buf, capacity, size_out, thread_id, output_base,
                               layout, pipeline_latency, false, 1U);
}
