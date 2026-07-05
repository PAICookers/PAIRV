#include "artifact_reader.h"

#include <flatbuffers/flatbuffers.h>

#include "compile_artifacts_generated.h"
#include "debug.h"

namespace fbs = paibox::backendv2::generated::fbs;

namespace
{
constexpr const char *kRvrtDebugTitle = "rvrt";
constexpr uint32_t kCapacityRxMargin = 1U;
constexpr uint32_t kCapacityWorkspaceFrameCap = 16U;

const fbs::CompileArtifacts *root_from_artifact(const rvrt_artifact_t *artifact)
{
    if ((artifact == nullptr) || (artifact->root == nullptr)) {
        return nullptr;
    }
    return static_cast<const fbs::CompileArtifacts *>(artifact->root);
}

rvrt_artifact_status_t config_frames(const rvrt_artifact_t *artifact,
                                     const fbs::ConfigFrames **frames)
{
    if ((artifact == nullptr) || (frames == nullptr)) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *root = root_from_artifact(artifact);
    if (root == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *config = root->config_frames();
    if (config == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *frames = config;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t config_words(const rvrt_artifact_t *artifact,
                                    const flatbuffers::Vector<uint32_t> **words)
{
    const fbs::ConfigFrames *config = nullptr;
    const auto status = config_frames(artifact, &config);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *config_words = config->words();
    if (config_words == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *words = config_words;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t thread_at(const rvrt_artifact_t *artifact,
                                 uint32_t thread_index,
                                 const fbs::ThreadIOMapping **thread)
{
    if ((artifact == nullptr) || (thread == nullptr)) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *root = root_from_artifact(artifact);
    if (root == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *io_mapping = root->io_mapping();
    if (io_mapping == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    const auto *threads = io_mapping->threads();
    if (threads == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    if (thread_index >= threads->size()) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    const auto *selected = threads->Get(thread_index);
    if (selected == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *thread = selected;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t input_at(const rvrt_artifact_t *artifact,
                                uint32_t thread_index, uint32_t input_index,
                                const fbs::InputTensorMapping **input)
{
    const fbs::ThreadIOMapping *thread = nullptr;
    auto status = thread_at(artifact, thread_index, &thread);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *mappings = thread->input_mappings();
    if (mappings == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    const auto *items = mappings->items();
    if (items == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    if (input_index >= items->size()) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    const auto *selected = items->Get(input_index);
    if (selected == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *input = selected;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t output_at(const rvrt_artifact_t *artifact,
                                 uint32_t thread_index, uint32_t output_index,
                                 const fbs::OutputTensorMapping **output)
{
    const fbs::ThreadIOMapping *thread = nullptr;
    auto status = thread_at(artifact, thread_index, &thread);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *mappings = thread->output_mappings();
    if (mappings == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    const auto *items = mappings->items();
    if (items == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    if (output_index >= items->size()) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    const auto *selected = items->Get(output_index);
    if (selected == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *output = selected;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t input_entry_at(const rvrt_artifact_t *artifact,
                                      uint32_t thread_index,
                                      uint32_t input_index,
                                      uint32_t entry_index,
                                      const fbs::InputEntry **entry)
{
    if (entry == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::InputTensorMapping *input = nullptr;
    auto status = input_at(artifact, thread_index, input_index, &input);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *entries = input->entries();
    if (entries == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }
    if (entry_index >= entries->size()) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    const auto *selected = entries->Get(entry_index);
    if (selected == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *entry = selected;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t output_entry_at(const rvrt_artifact_t *artifact,
                                       uint32_t thread_index,
                                       uint32_t output_index,
                                       uint32_t entry_index,
                                       const fbs::OutputEntry **entry)
{
    if (entry == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::OutputTensorMapping *output = nullptr;
    auto status = output_at(artifact, thread_index, output_index, &output);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *entries = output->entries();
    if (entries == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }
    if (entry_index >= entries->size()) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    const auto *selected = entries->Get(entry_index);
    if (selected == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *entry = selected;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
fill_core_offset(const fbs::CoreOffset *source,
                 rvrt_artifact_core_offset_t *core_offset)
{
    if (core_offset == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    if (source == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    core_offset->xy = source->xy();
    core_offset->x = source->x();
    core_offset->y = source->y();
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t fill_copy_count(const fbs::CopyCount *source,
                                       rvrt_artifact_copy_count_t *copy_count)
{
    if (copy_count == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    if (source == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    copy_count->xy = source->xy();
    copy_count->x = source->x();
    copy_count->y = source->y();
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t shape_size(const fbs::Shape *shape,
                                  const flatbuffers::Vector<int32_t> **size)
{
    if (size == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    if (shape == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    const auto *dims = shape->size();
    if (dims == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *size = dims;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t tensor_bytes(const rvrt_artifact_t *artifact,
                                    uint32_t thread_index,
                                    uint32_t tensor_index, bool input,
                                    uint32_t *bytes)
{
    if (bytes == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const flatbuffers::Vector<int32_t> *dims = nullptr;
    uint32_t bit_width = 0U;
    rvrt_artifact_status_t status = RVRT_ARTIFACT_OK;
    if (input) {
        const fbs::InputTensorMapping *mapping = nullptr;
        status = input_at(artifact, thread_index, tensor_index, &mapping);
        if (status != RVRT_ARTIFACT_OK) {
            return status;
        }
        bit_width = mapping->bit_width();
        status = shape_size(mapping->shape(), &dims);
    } else {
        const fbs::OutputTensorMapping *mapping = nullptr;
        status = output_at(artifact, thread_index, tensor_index, &mapping);
        if (status != RVRT_ARTIFACT_OK) {
            return status;
        }
        bit_width = mapping->bit_width();
        status = shape_size(mapping->shape(), &dims);
    }
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    if (bit_width > 8U) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    uint32_t product = 1U;
    for (uint32_t i = 0U; i < dims->size(); ++i) {
        const int32_t dim = dims->Get(i);
        if ((dim <= 0) ||
            (product > (UINT32_MAX / static_cast<uint32_t>(dim)))) {
            return RVRT_ARTIFACT_OUT_OF_RANGE;
        }
        product *= static_cast<uint32_t>(dim);
    }

    *bytes = product;
    return RVRT_ARTIFACT_OK;
}

bool is_aligned(const void *data)
{
    return (reinterpret_cast<uintptr_t>(data) &
            (static_cast<uintptr_t>(RVRT_ARTIFACT_ALIGNMENT) - 1U)) == 0U;
}
} // namespace

rvrt_artifact_status_t rvrt_artifact_read(const uint8_t *data, size_t size,
                                          rvrt_artifact_t *artifact)
{
    if ((data == nullptr) || (artifact == nullptr) || (size == 0U)) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    artifact->data = nullptr;
    artifact->size = 0U;
    artifact->root = nullptr;

    if (!is_aligned(data)) {
        RV_DEBUG_LOGE(kRvrtDebugTitle, "artifact data is not %u-byte aligned",
                      static_cast<unsigned>(RVRT_ARTIFACT_ALIGNMENT));
        return RVRT_ARTIFACT_BAD_ALIGNMENT;
    }

    flatbuffers::Verifier verifier(data, size);
    if (!fbs::VerifyCompileArtifactsBuffer(verifier)) {
        RV_DEBUG_LOGE(kRvrtDebugTitle, "artifact verifier failed, bytes=%u",
                      static_cast<unsigned>(size));
        return RVRT_ARTIFACT_VERIFY_FAILED;
    }

    const auto *root = fbs::GetCompileArtifacts(data);
    const auto *io_mapping = root->io_mapping();
    const auto *config = root->config_frames();
    if ((io_mapping == nullptr) || (io_mapping->threads() == nullptr) ||
        (config == nullptr) || (config->words() == nullptr)) {
        RV_DEBUG_LOGE(kRvrtDebugTitle, "artifact missing required tables");
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    artifact->data = data;
    artifact->size = size;
    artifact->root = root;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t rvrt_artifact_get_info(const rvrt_artifact_t *artifact,
                                              rvrt_artifact_info_t *info)
{
    if (info == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *root = root_from_artifact(artifact);
    if (root == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    info->schema_version = root->schema_version();

    auto status =
        rvrt_artifact_config_word_count(artifact, &info->config_word_count);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const fbs::ConfigFrames *config = nullptr;
    status = config_frames(artifact, &config);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    info->config_word_order = static_cast<uint32_t>(config->word_order());

    return rvrt_artifact_thread_count(artifact, &info->thread_count);
}

rvrt_artifact_status_t
rvrt_artifact_get_capacity(const rvrt_artifact_t *artifact,
                           uint32_t thread_index, uint32_t input_index,
                           uint32_t output_index,
                           rvrt_artifact_capacity_t *capacity)
{
    if (capacity == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    *capacity = {};
    auto status = tensor_bytes(artifact, thread_index, input_index, true,
                               &capacity->input_bytes);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    status = tensor_bytes(artifact, thread_index, output_index, false,
                          &capacity->output_bytes);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    status = rvrt_artifact_output_entry_count(
        artifact, thread_index, output_index, &capacity->output_entry_count);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    if (capacity->output_entry_count > (UINT32_MAX - kCapacityRxMargin)) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }
    capacity->rx_frame_count = capacity->output_entry_count + kCapacityRxMargin;

    uint32_t input_entry_count = 0U;
    status = rvrt_artifact_input_entry_count(artifact, thread_index,
                                             input_index, &input_entry_count);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    if (input_entry_count == 0U) {
        capacity->workspace_frame_count = 1U;
    } else if (input_entry_count < kCapacityWorkspaceFrameCap) {
        capacity->workspace_frame_count = input_entry_count;
    } else {
        capacity->workspace_frame_count = kCapacityWorkspaceFrameCap;
    }
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_config_word_count(const rvrt_artifact_t *artifact,
                                uint32_t *count)
{
    if (count == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const flatbuffers::Vector<uint32_t> *words = nullptr;
    const auto status = config_words(artifact, &words);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    *count = static_cast<uint32_t>(words->size());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_config_word(const rvrt_artifact_t *artifact, uint32_t word_index,
                          uint32_t *word)
{
    if (word == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const flatbuffers::Vector<uint32_t> *words = nullptr;
    const auto status = config_words(artifact, &words);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    if (word_index >= words->size()) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    *word = words->Get(word_index);
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_thread_count(const rvrt_artifact_t *artifact, uint32_t *count)
{
    if (count == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *root = root_from_artifact(artifact);
    if (root == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *io_mapping = root->io_mapping();
    if ((io_mapping == nullptr) || (io_mapping->threads() == nullptr)) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *count = static_cast<uint32_t>(io_mapping->threads()->size());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t rvrt_artifact_thread_root_core_offset(
    const rvrt_artifact_t *artifact, uint32_t thread_index,
    rvrt_artifact_core_offset_t *root_core_offset)
{
    const fbs::ThreadIOMapping *thread = nullptr;
    const auto status = thread_at(artifact, thread_index, &thread);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    return fill_core_offset(thread->root_core_offset(), root_core_offset);
}

rvrt_artifact_status_t
rvrt_artifact_thread_runtime(const rvrt_artifact_t *artifact,
                             uint32_t thread_index,
                             rvrt_artifact_runtime_t *runtime)
{
    if (runtime == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ThreadIOMapping *thread = nullptr;
    const auto status = thread_at(artifact, thread_index, &thread);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *source = thread->runtime();
    if (source == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    runtime->timesteps = source->timesteps();
    runtime->tick_depth = source->tick_depth();
    runtime->sync_steps = source->sync_steps();
    runtime->decode_mode = static_cast<uint32_t>(source->decode_mode());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_input_bit_width(const rvrt_artifact_t *artifact,
                              uint32_t thread_index, uint32_t input_index,
                              uint32_t *bit_width)
{
    if (bit_width == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::InputTensorMapping *input = nullptr;
    const auto status = input_at(artifact, thread_index, input_index, &input);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    *bit_width = input->bit_width();
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_output_bit_width(const rvrt_artifact_t *artifact,
                               uint32_t thread_index, uint32_t output_index,
                               uint32_t *bit_width)
{
    if (bit_width == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::OutputTensorMapping *output = nullptr;
    const auto status =
        output_at(artifact, thread_index, output_index, &output);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    *bit_width = output->bit_width();
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_output_kind(const rvrt_artifact_t *artifact,
                          uint32_t thread_index, uint32_t output_index,
                          uint32_t *kind)
{
    if (kind == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::OutputTensorMapping *output = nullptr;
    const auto status =
        output_at(artifact, thread_index, output_index, &output);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    *kind = static_cast<uint32_t>(output->kind());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_input_entry_count(const rvrt_artifact_t *artifact,
                                uint32_t thread_index, uint32_t input_index,
                                uint32_t *count)
{
    if (count == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::InputTensorMapping *input = nullptr;
    const auto status = input_at(artifact, thread_index, input_index, &input);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *entries = input->entries();
    if (entries == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *count = static_cast<uint32_t>(entries->size());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_output_entry_count(const rvrt_artifact_t *artifact,
                                 uint32_t thread_index, uint32_t output_index,
                                 uint32_t *count)
{
    if (count == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::OutputTensorMapping *output = nullptr;
    const auto status =
        output_at(artifact, thread_index, output_index, &output);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *entries = output->entries();
    if (entries == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *count = static_cast<uint32_t>(entries->size());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_input_entry(const rvrt_artifact_t *artifact,
                          uint32_t thread_index, uint32_t input_index,
                          uint32_t entry_index,
                          rvrt_artifact_input_entry_t *entry)
{
    if (entry == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::InputEntry *source = nullptr;
    auto status = input_entry_at(artifact, thread_index, input_index,
                                 entry_index, &source);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    entry->elem_idx = source->elem_idx();
    status = fill_core_offset(source->core_offset(), &entry->core_offset);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    status = fill_copy_count(source->copy_count(), &entry->copy_count);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    entry->tick_relative = source->tick_relative();
    entry->addr_axon = source->addr_axon();
    entry->target_lcn = source->target_lcn();
    entry->copy_id = source->copy_id();
    entry->dtype = static_cast<uint32_t>(source->dtype());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_output_entry(const rvrt_artifact_t *artifact,
                           uint32_t thread_index, uint32_t output_index,
                           uint32_t entry_index,
                           rvrt_artifact_output_entry_t *entry)
{
    if (entry == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::OutputEntry *source = nullptr;
    const auto status = output_entry_at(artifact, thread_index, output_index,
                                        entry_index, &source);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    entry->elem_idx = source->elem_idx();
    entry->copy_id = source->copy_id();
    entry->axon_bit_idx = source->axon_bit_idx();
    entry->dtype = static_cast<uint32_t>(source->dtype());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_output_target_lcn(const rvrt_artifact_t *artifact,
                                uint32_t thread_index, uint32_t *target_lcn)
{
    if (target_lcn == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ThreadIOMapping *thread = nullptr;
    const auto status = thread_at(artifact, thread_index, &thread);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *mappings = thread->output_mappings();
    if (mappings == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *target_lcn = mappings->target_lcn();
    return RVRT_ARTIFACT_OK;
}

const char *rvrt_artifact_status_string(rvrt_artifact_status_t status)
{
    switch (status) {
        case RVRT_ARTIFACT_OK:
            return "ok";
        case RVRT_ARTIFACT_NULL_ARGUMENT:
            return "null argument";
        case RVRT_ARTIFACT_VERIFY_FAILED:
            return "verify failed";
        case RVRT_ARTIFACT_MISSING_FIELD:
            return "missing field";
        case RVRT_ARTIFACT_OUT_OF_RANGE:
            return "out of range";
        case RVRT_ARTIFACT_BAD_ALIGNMENT:
            return "bad alignment";
        default:
            return "unknown";
    }
}
