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
