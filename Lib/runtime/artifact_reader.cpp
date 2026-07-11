#include "artifact_reader.h"
#include "debug.h"
#include "generated/compile_artifacts_generated.h"
#include <flatbuffers/flatbuffers.h>

namespace fbs = paibox::backendv2::generated::fbs;

namespace
{
constexpr const char *kRvrtDebugTitle = "rvrt";
constexpr uint32_t kSupportedSchemaVersion = 1U;
constexpr uint32_t kCapacityRxMargin = 1U;
constexpr uint32_t kCapacityWorkspaceFrameCap = 16U;
constexpr uint32_t kControlPayloadMax = 0xFFFFFFU;
constexpr uint32_t kTargetLcnMax = 7U;

const fbs::CompileArtifacts *root_from_artifact(const rvrt_artifact_t *artifact)
{
    if ((artifact == nullptr) || (artifact->root == nullptr)) {
        return nullptr;
    }
    return static_cast<const fbs::CompileArtifacts *>(artifact->root);
}

/** @brief Return the execution-plan table borrowed from a verified artifact. */
rvrt_artifact_status_t execution_plan_at(const rvrt_artifact_t *artifact,
                                         const fbs::ExecutionPlan **plan)
{
    if (plan == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *root = root_from_artifact(artifact);
    if (root == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *source = root->execution_plan();
    if (source == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *plan = source;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t config_frames(const rvrt_artifact_t *artifact,
                                     const fbs::ConfigFrames **frames)
{
    if ((artifact == nullptr) || (frames == nullptr)) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *config =
        static_cast<const fbs::ConfigFrames *>(artifact->config_frames);
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

    const auto *io_mapping =
        static_cast<const fbs::IOMapping *>(artifact->io_mapping);
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

rvrt_artifact_status_t cpu_task_at(const rvrt_artifact_t *artifact,
                                   uint32_t task_index,
                                   const fbs::CpuTask **task)
{
    if (task == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ExecutionPlan *plan = nullptr;
    auto status = execution_plan_at(artifact, &plan);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *tasks = plan->cpu_tasks();
    if (tasks == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }
    if (task_index >= tasks->size()) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    const auto *selected = tasks->Get(task_index);
    if (selected == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *task = selected;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t stage_at(const rvrt_artifact_t *artifact,
                                uint32_t stage_index,
                                const fbs::ExecutionStage **stage)
{
    if (stage == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ExecutionPlan *plan = nullptr;
    auto status = execution_plan_at(artifact, &plan);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *stages = plan->stages();
    if (stages == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }
    if (stage_index >= stages->size()) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    const auto *selected = stages->Get(stage_index);
    if (selected == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *stage = selected;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t paicore_phase_at(const rvrt_artifact_t *artifact,
                                        uint32_t phase_index,
                                        const fbs::PaicorePhase **phase)
{
    if (phase == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ExecutionPlan *plan = nullptr;
    auto status = execution_plan_at(artifact, &plan);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *phases = plan->paicore_phases();
    if (phases == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }
    if (phase_index >= phases->size()) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    const auto *selected = phases->Get(phase_index);
    if (selected == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    *phase = selected;
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

/**
 * @brief Validate a shape and derive its rank, dimensions, and element count.
 *
 * Empty rank represents a scalar and therefore has one logical element.
 */
rvrt_artifact_status_t copy_shape(const fbs::Shape *shape, uint32_t *rank,
                                  int32_t *dims, uint32_t *numel)
{
    if ((rank == nullptr) || (dims == nullptr) || (numel == nullptr)) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    if (shape == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    const auto *source = shape->size();
    if (source == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }
    if (source->size() > RVRT_ARTIFACT_MAX_RANK) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    uint32_t product = 1U;
    for (uint32_t i = 0U; i < source->size(); ++i) {
        const int32_t dim = source->Get(i);
        if ((dim <= 0) ||
            (product > (UINT32_MAX / static_cast<uint32_t>(dim)))) {
            return RVRT_ARTIFACT_OUT_OF_RANGE;
        }
        dims[i] = dim;
        product *= static_cast<uint32_t>(dim);
    }
    for (uint32_t i = source->size(); i < RVRT_ARTIFACT_MAX_RANK; ++i) {
        dims[i] = 0;
    }
    *rank = static_cast<uint32_t>(source->size());
    *numel = product;
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t mapping_dtype_bits(fbs::DataType dtype, uint32_t *bits)
{
    if (bits == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    switch (dtype) {
        case fbs::DataType_UINT1:
        case fbs::DataType_INT1:
            *bits = 1U;
            return RVRT_ARTIFACT_OK;
        case fbs::DataType_UINT2:
        case fbs::DataType_INT2:
            *bits = 2U;
            return RVRT_ARTIFACT_OK;
        case fbs::DataType_UINT4:
        case fbs::DataType_INT4:
            *bits = 4U;
            return RVRT_ARTIFACT_OK;
        case fbs::DataType_UINT8:
        case fbs::DataType_INT8:
            *bits = 8U;
            return RVRT_ARTIFACT_OK;
        default:
            return RVRT_ARTIFACT_OUT_OF_RANGE;
    }
}

rvrt_artifact_status_t
validate_input_mapping(const fbs::InputTensorMapping *mapping)
{
    if ((mapping == nullptr) || (mapping->shape() == nullptr) ||
        (mapping->entries() == nullptr)) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    uint32_t rank = 0U;
    uint32_t numel = 0U;
    int32_t shape[RVRT_ARTIFACT_MAX_RANK] = {};
    auto status = copy_shape(mapping->shape(), &rank, shape, &numel);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    for (const auto *entry : *mapping->entries()) {
        if ((entry == nullptr) || (entry->core_offset() == nullptr) ||
            (entry->copy_count() == nullptr)) {
            return RVRT_ARTIFACT_MISSING_FIELD;
        }
        if ((entry->elem_idx() >= numel) ||
            (entry->target_lcn() > kTargetLcnMax)) {
            return RVRT_ARTIFACT_OUT_OF_RANGE;
        }
        uint32_t bits = 0U;
        status = mapping_dtype_bits(entry->dtype(), &bits);
        if ((status != RVRT_ARTIFACT_OK) || (bits != mapping->bit_width())) {
            return RVRT_ARTIFACT_BAD_VALUE;
        }
    }
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
validate_output_mapping(const fbs::OutputTensorMapping *mapping)
{
    if ((mapping == nullptr) || (mapping->shape() == nullptr) ||
        (mapping->entries() == nullptr)) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    uint32_t rank = 0U;
    uint32_t numel = 0U;
    int32_t shape[RVRT_ARTIFACT_MAX_RANK] = {};
    auto status = copy_shape(mapping->shape(), &rank, shape, &numel);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    bool first = true;
    uint32_t previous_key = 0U;
    for (const auto *entry : *mapping->entries()) {
        if (entry == nullptr) {
            return RVRT_ARTIFACT_MISSING_FIELD;
        }
        if (entry->elem_idx() >= numel) {
            return RVRT_ARTIFACT_OUT_OF_RANGE;
        }
        if (!first && (entry->axon_bit_idx() <= previous_key)) {
            return RVRT_ARTIFACT_BAD_VALUE;
        }
        first = false;
        previous_key = entry->axon_bit_idx();

        if (mapping->kind() == fbs::OutputKind_DATA) {
            uint32_t bits = 0U;
            status = mapping_dtype_bits(entry->dtype(), &bits);
            if ((status != RVRT_ARTIFACT_OK) ||
                (bits != mapping->bit_width())) {
                return RVRT_ARTIFACT_BAD_VALUE;
            }
        } else if ((mapping->kind() != fbs::OutputKind_VOLTAGE) ||
                   (mapping->bit_width() != 32U)) {
            return RVRT_ARTIFACT_BAD_VALUE;
        }
    }
    return RVRT_ARTIFACT_OK;
}

/** @brief Validate mapping fields required by the C frame codec. */
rvrt_artifact_status_t validate_io_mapping(const fbs::IOMapping *io_mapping)
{
    if ((io_mapping == nullptr) || (io_mapping->threads() == nullptr)) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }
    for (const auto *thread : *io_mapping->threads()) {
        if ((thread == nullptr) || (thread->input_mappings() == nullptr) ||
            (thread->output_mappings() == nullptr) ||
            (thread->input_mappings()->items() == nullptr) ||
            (thread->output_mappings()->items() == nullptr)) {
            return RVRT_ARTIFACT_MISSING_FIELD;
        }
        if (thread->output_mappings()->target_lcn() > kTargetLcnMax) {
            return RVRT_ARTIFACT_OUT_OF_RANGE;
        }
        for (const auto *mapping : *thread->input_mappings()->items()) {
            const auto status = validate_input_mapping(mapping);
            if (status != RVRT_ARTIFACT_OK) {
                return status;
            }
        }
        for (const auto *mapping : *thread->output_mappings()->items()) {
            const auto status = validate_output_mapping(mapping);
            if (status != RVRT_ARTIFACT_OK) {
                return status;
            }
        }
    }
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t dtype_size(uint32_t dtype, uint32_t *bytes)
{
    if (bytes == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    switch (dtype) {
        case static_cast<uint32_t>(fbs::DataType_UINT1):
        case static_cast<uint32_t>(fbs::DataType_INT1):
        case static_cast<uint32_t>(fbs::DataType_UINT2):
        case static_cast<uint32_t>(fbs::DataType_INT2):
        case static_cast<uint32_t>(fbs::DataType_UINT4):
        case static_cast<uint32_t>(fbs::DataType_INT4):
        case static_cast<uint32_t>(fbs::DataType_UINT8):
        case static_cast<uint32_t>(fbs::DataType_INT8):
            *bytes = 1U;
            return RVRT_ARTIFACT_OK;
        case static_cast<uint32_t>(fbs::DataType_INT64):
            *bytes = 8U;
            return RVRT_ARTIFACT_OK;
        default:
            return RVRT_ARTIFACT_OUT_OF_RANGE;
    }
}

rvrt_artifact_status_t runtime_buffer_bytes(const rvrt_artifact_t *artifact,
                                            uint32_t buffer_index,
                                            uint32_t *bytes, uint32_t *dtype,
                                            uint32_t *rank, int32_t *shape)
{
    const fbs::ExecutionPlan *plan = nullptr;
    auto status = execution_plan_at(artifact, &plan);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    const auto *buffers = plan->buffers();
    if (buffers == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }
    if (buffer_index >= buffers->size()) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }
    const auto *buffer = buffers->Get(buffer_index);
    if (buffer == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    const uint32_t buffer_dtype = static_cast<uint32_t>(buffer->dtype());
    uint32_t elem_bytes = 0U;
    status = dtype_size(buffer_dtype, &elem_bytes);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    uint32_t numel = 0U;
    uint32_t local_rank = 0U;
    int32_t local_shape[RVRT_ARTIFACT_MAX_RANK] = {};
    status = copy_shape(buffer->shape(), &local_rank, local_shape, &numel);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    if (numel > (UINT32_MAX / elem_bytes)) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    if (bytes != nullptr) {
        *bytes = numel * elem_bytes;
    }
    if (dtype != nullptr) {
        *dtype = buffer_dtype;
    }
    if (rank != nullptr) {
        *rank = local_rank;
    }
    if (shape != nullptr) {
        for (uint32_t i = 0U; i < RVRT_ARTIFACT_MAX_RANK; ++i) {
            shape[i] = local_shape[i];
        }
    }
    return RVRT_ARTIFACT_OK;
}

bool is_aligned(const void *data)
{
    return (reinterpret_cast<uintptr_t>(data) &
            (static_cast<uintptr_t>(RVRT_ARTIFACT_ALIGNMENT) - 1U)) == 0U;
}
} // namespace

rvrt_artifact_status_t
rvrt_artifact_stage_buffer_refs(const rvrt_artifact_t *artifact,
                                const rvrt_artifact_stage_t *stage,
                                uint32_t *input_ref, uint32_t *output_ref)
{
    if ((artifact == nullptr) || (stage == nullptr) || (input_ref == nullptr) ||
        (output_ref == nullptr)) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    if (stage->kind == RVRT_STAGE_PAICORE) {
        rvrt_artifact_paicore_phase_t phase{};
        const auto status =
            rvrt_artifact_paicore_phase(artifact, stage->ref_index, &phase);
        if (status != RVRT_ARTIFACT_OK) {
            return status;
        }
        *input_ref = phase.input_ref;
        *output_ref = phase.output_ref;
        return RVRT_ARTIFACT_OK;
    }

    if (stage->kind == RVRT_STAGE_CPU_TASK) {
        rvrt_artifact_cpu_task_t task{};
        const auto status =
            rvrt_artifact_cpu_task(artifact, stage->ref_index, &task);
        if (status != RVRT_ARTIFACT_OK) {
            return status;
        }
        *input_ref = task.input_ref;
        *output_ref = task.output_ref;
        return RVRT_ARTIFACT_OK;
    }

    return RVRT_ARTIFACT_BAD_VALUE;
}

rvrt_artifact_status_t rvrt_artifact_read(const uint8_t *data, size_t size,
                                          rvrt_artifact_t *artifact)
{
    if ((data == nullptr) || (artifact == nullptr)) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    if (size == 0U) {
        return RVRT_ARTIFACT_BAD_VALUE;
    }

    artifact->root = nullptr;
    artifact->io_mapping = nullptr;
    artifact->config_frames = nullptr;

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
    if (root->schema_version() != kSupportedSchemaVersion) {
        RV_DEBUG_LOGE(kRvrtDebugTitle,
                      "unsupported artifact schema version=%u expected=%u",
                      static_cast<unsigned>(root->schema_version()),
                      static_cast<unsigned>(kSupportedSchemaVersion));
        return RVRT_ARTIFACT_BAD_VALUE;
    }
    const auto *io_mapping = root->io_mapping();
    const auto *config = root->config_frames();
    const auto *plan = root->execution_plan();
    const auto *target = (plan == nullptr) ? nullptr : plan->runtime_target();
    if ((target == nullptr) || (target->target_id() == nullptr) ||
        (target->profile_id() == nullptr) || (plan->buffers() == nullptr) ||
        (plan->cpu_tasks() == nullptr) || (plan->stages() == nullptr) ||
        (plan->paicore_phases() == nullptr)) {
        RV_DEBUG_LOGE(kRvrtDebugTitle, "artifact missing required tables");
        return RVRT_ARTIFACT_MISSING_FIELD;
    }
    if ((io_mapping == nullptr) || (io_mapping->threads() == nullptr) ||
        (config == nullptr) || (config->words() == nullptr)) {
        RV_DEBUG_LOGE(kRvrtDebugTitle, "artifact missing root runtime tables");
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    const auto mapping_status = validate_io_mapping(io_mapping);
    if (mapping_status != RVRT_ARTIFACT_OK) {
        RV_DEBUG_LOGE(kRvrtDebugTitle, "artifact mapping validation failed");
        return mapping_status;
    }

    artifact->root = root;
    artifact->io_mapping = io_mapping;
    artifact->config_frames = config;
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
                           uint32_t thread_index,
                           rvrt_artifact_capacity_t *capacity)
{
    if (capacity == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    *capacity = {};

    uint32_t stage_count = 0U;
    auto status = rvrt_artifact_stage_count(artifact, &stage_count);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    if (stage_count == 0U) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    rvrt_artifact_stage_t first_stage{};
    status = rvrt_artifact_stage(artifact, 0U, &first_stage);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    rvrt_artifact_stage_t final_stage{};
    status = rvrt_artifact_stage(artifact, stage_count - 1U, &final_stage);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    uint32_t input_ref = 0U;
    uint32_t ignored_ref = 0U;
    status = rvrt_artifact_stage_buffer_refs(artifact, &first_stage, &input_ref,
                                             &ignored_ref);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    uint32_t final_output_ref = 0U;
    status = rvrt_artifact_stage_buffer_refs(artifact, &final_stage,
                                             &ignored_ref, &final_output_ref);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    status = runtime_buffer_bytes(artifact, input_ref, &capacity->input_bytes,
                                  nullptr, nullptr, nullptr);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    status = runtime_buffer_bytes(
        artifact, final_output_ref, &capacity->final_output_bytes,
        &capacity->final_output_dtype, &capacity->final_output_rank,
        capacity->final_output_shape);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    uint32_t phase_count = 0U;
    status = rvrt_artifact_paicore_phase_count(artifact, &phase_count);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    if (phase_count == 0U) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    uint32_t max_input_entries = 0U;
    uint32_t max_output_entries = 0U;
    for (uint32_t i = 0U; i < phase_count; ++i) {
        rvrt_artifact_paicore_phase_t phase{};
        status = rvrt_artifact_paicore_phase(artifact, i, &phase);
        if (status != RVRT_ARTIFACT_OK) {
            return status;
        }

        const fbs::InputTensorMapping *input_mapping = nullptr;
        status = input_at(artifact, thread_index, phase.input_mapping_ref,
                          &input_mapping);
        if (status != RVRT_ARTIFACT_OK) {
            return status;
        }
        const auto *input_entries = input_mapping->entries();
        if (input_entries == nullptr) {
            return RVRT_ARTIFACT_MISSING_FIELD;
        }
        uint32_t entry_count = static_cast<uint32_t>(input_entries->size());
        if (entry_count > max_input_entries) {
            max_input_entries = entry_count;
        }

        const fbs::OutputTensorMapping *output_mapping = nullptr;
        status = output_at(artifact, thread_index, phase.output_mapping_ref,
                           &output_mapping);
        if (status != RVRT_ARTIFACT_OK) {
            return status;
        }
        const auto *output_entries = output_mapping->entries();
        if (output_entries == nullptr) {
            return RVRT_ARTIFACT_MISSING_FIELD;
        }
        entry_count = static_cast<uint32_t>(output_entries->size());
        if (entry_count > max_output_entries) {
            max_output_entries = entry_count;
        }
    }

    if (max_output_entries > (UINT32_MAX - kCapacityRxMargin)) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }
    capacity->rx_frame_count = max_output_entries + kCapacityRxMargin;
    capacity->workspace_frame_count =
        (max_input_entries == 0U)
            ? 1U
            : ((max_input_entries < kCapacityWorkspaceFrameCap)
                   ? max_input_entries
                   : kCapacityWorkspaceFrameCap);
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_runtime_target(const rvrt_artifact_t *artifact,
                             rvrt_artifact_runtime_target_t *target)
{
    if (target == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ExecutionPlan *plan = nullptr;
    auto status = execution_plan_at(artifact, &plan);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *source = plan->runtime_target();
    if (source == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    target->target_id = source->target_id()->c_str();
    target->profile_id = source->profile_id()->c_str();
    target->required_task_abi_version = source->required_task_abi_version();
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_cpu_task_count(const rvrt_artifact_t *artifact, uint32_t *count)
{
    if (count == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ExecutionPlan *plan = nullptr;
    auto status = execution_plan_at(artifact, &plan);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *tasks = plan->cpu_tasks();
    if (tasks == nullptr) {
        *count = 0U;
        return RVRT_ARTIFACT_OK;
    }

    *count = static_cast<uint32_t>(tasks->size());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t rvrt_artifact_cpu_task(const rvrt_artifact_t *artifact,
                                              uint32_t task_index,
                                              rvrt_artifact_cpu_task_t *task)
{
    if (task == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::CpuTask *source = nullptr;
    const auto status = cpu_task_at(artifact, task_index, &source);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    task->input_ref = source->input_ref();
    task->output_ref = source->output_ref();
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_stage_count(const rvrt_artifact_t *artifact, uint32_t *count)
{
    if (count == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ExecutionPlan *plan = nullptr;
    auto status = execution_plan_at(artifact, &plan);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *stages = plan->stages();
    if (stages == nullptr) {
        *count = 0U;
        return RVRT_ARTIFACT_OK;
    }

    *count = static_cast<uint32_t>(stages->size());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t rvrt_artifact_stage(const rvrt_artifact_t *artifact,
                                           uint32_t stage_index,
                                           rvrt_artifact_stage_t *stage)
{
    if (stage == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ExecutionStage *source = nullptr;
    const auto status = stage_at(artifact, stage_index, &source);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    stage->stage_index = source->stage_index();
    stage->kind = static_cast<uint32_t>(source->kind());
    stage->ref_index = source->ref_index();
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_paicore_phase_count(const rvrt_artifact_t *artifact,
                                  uint32_t *count)
{
    if (count == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ExecutionPlan *plan = nullptr;
    auto status = execution_plan_at(artifact, &plan);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *phases = plan->paicore_phases();
    if (phases == nullptr) {
        *count = 0U;
        return RVRT_ARTIFACT_OK;
    }

    *count = static_cast<uint32_t>(phases->size());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_paicore_phase(const rvrt_artifact_t *artifact,
                            uint32_t phase_index,
                            rvrt_artifact_paicore_phase_t *phase)
{
    if (phase == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::PaicorePhase *source = nullptr;
    const auto status = paicore_phase_at(artifact, phase_index, &source);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    phase->input_ref = source->input_ref();
    phase->output_ref = source->output_ref();
    phase->input_mapping_ref = source->input_mapping_ref();
    phase->output_mapping_ref = source->output_mapping_ref();
    phase->latency_ticks = source->latency_ticks();
    if (phase->latency_ticks == 0U) {
        return RVRT_ARTIFACT_BAD_VALUE;
    }
    if (phase->latency_ticks > kControlPayloadMax) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_runtime_buffer_count(const rvrt_artifact_t *artifact,
                                   uint32_t *count)
{
    if (count == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ExecutionPlan *plan = nullptr;
    auto status = execution_plan_at(artifact, &plan);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }

    const auto *buffers = plan->buffers();
    if (buffers == nullptr) {
        *count = 0U;
        return RVRT_ARTIFACT_OK;
    }

    *count = static_cast<uint32_t>(buffers->size());
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t rvrt_artifact_runtime_buffer_bytes(
    const rvrt_artifact_t *artifact, uint32_t buffer_index, uint32_t *bytes,
    uint32_t *dtype, uint32_t *rank, int32_t *shape)
{
    return runtime_buffer_bytes(artifact, buffer_index, bytes, dtype, rank,
                                shape);
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
rvrt_artifact_config_frame_words(const rvrt_artifact_t *artifact,
                                 uint32_t frame_index, uint32_t *high,
                                 uint32_t *low)
{
    if ((high == nullptr) || (low == nullptr)) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const fbs::ConfigFrames *config = nullptr;
    auto status = config_frames(artifact, &config);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    const auto *words = config->words();
    if (words == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    if (frame_index >= (words->size() / 2U)) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }
    const uint32_t word_index = frame_index * 2U;

    const uint32_t word0 = words->Get(word_index);
    const uint32_t word1 = words->Get(word_index + 1U);
    if (config->word_order() == fbs::WordOrder_HIGH_FIRST) {
        *high = word0;
        *low = word1;
    } else if (config->word_order() == fbs::WordOrder_LOW_FIRST) {
        *high = word1;
        *low = word0;
    } else {
        return RVRT_ARTIFACT_BAD_VALUE;
    }
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t
rvrt_artifact_thread_count(const rvrt_artifact_t *artifact, uint32_t *count)
{
    if (count == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    if (artifact == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }

    const auto *io_mapping =
        static_cast<const fbs::IOMapping *>(artifact->io_mapping);
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

rvrt_artifact_status_t rvrt_artifact_get_input_mapping_view(
    const rvrt_artifact_t *artifact, uint32_t thread_index,
    uint32_t input_index, rvrt_artifact_input_mapping_view_t *view)
{
    if (view == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    *view = {};

    const fbs::InputTensorMapping *input = nullptr;
    const auto status = input_at(artifact, thread_index, input_index, &input);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    const auto *entries = input->entries();
    if (entries == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    view->entries = entries;
    view->entry_count = static_cast<uint32_t>(entries->size());
    view->bit_width = input->bit_width();
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t rvrt_artifact_get_output_mapping_view(
    const rvrt_artifact_t *artifact, uint32_t thread_index,
    uint32_t output_index, rvrt_artifact_output_mapping_view_t *view)
{
    if (view == nullptr) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    *view = {};

    const fbs::ThreadIOMapping *thread = nullptr;
    auto status = thread_at(artifact, thread_index, &thread);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    const fbs::OutputTensorMapping *output = nullptr;
    status = output_at(artifact, thread_index, output_index, &output);
    if (status != RVRT_ARTIFACT_OK) {
        return status;
    }
    const auto *entries = output->entries();
    if ((entries == nullptr) || (thread->output_mappings() == nullptr)) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    view->entries = entries;
    view->entry_count = static_cast<uint32_t>(entries->size());
    view->bit_width = output->bit_width();
    view->kind = static_cast<uint32_t>(output->kind());
    view->target_lcn = thread->output_mappings()->target_lcn();
    return RVRT_ARTIFACT_OK;
}

rvrt_artifact_status_t rvrt_artifact_input_mapping_entry(
    const rvrt_artifact_input_mapping_view_t *view, uint32_t entry_index,
    rvrt_artifact_input_entry_t *entry)
{
    if ((view == nullptr) || (view->entries == nullptr) || (entry == nullptr)) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    if (entry_index >= view->entry_count) {
        return RVRT_ARTIFACT_OUT_OF_RANGE;
    }

    using Entries = flatbuffers::Vector<flatbuffers::Offset<fbs::InputEntry>>;
    const auto *entries = static_cast<const Entries *>(view->entries);
    const auto *source = entries->Get(entry_index);
    if (source == nullptr) {
        return RVRT_ARTIFACT_MISSING_FIELD;
    }

    entry->elem_idx = source->elem_idx();
    auto status = fill_core_offset(source->core_offset(), &entry->core_offset);
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

rvrt_artifact_status_t rvrt_artifact_output_mapping_find(
    const rvrt_artifact_output_mapping_view_t *view, uint32_t axon_bit_idx,
    rvrt_artifact_output_entry_t *entry, bool *found)
{
    if ((view == nullptr) || (view->entries == nullptr) || (entry == nullptr) ||
        (found == nullptr)) {
        return RVRT_ARTIFACT_NULL_ARGUMENT;
    }
    *found = false;

    using Entries = flatbuffers::Vector<flatbuffers::Offset<fbs::OutputEntry>>;
    const auto *entries = static_cast<const Entries *>(view->entries);
    const auto *candidate = entries->LookupByKey(axon_bit_idx);
    if (candidate == nullptr) {
        return RVRT_ARTIFACT_OK;
    }
    entry->elem_idx = candidate->elem_idx();
    entry->copy_id = candidate->copy_id();
    entry->axon_bit_idx = candidate->axon_bit_idx();
    entry->dtype = static_cast<uint32_t>(candidate->dtype());
    *found = true;
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
        case RVRT_ARTIFACT_BAD_VALUE:
            return "bad value";
        default:
            return "unknown";
    }
}
