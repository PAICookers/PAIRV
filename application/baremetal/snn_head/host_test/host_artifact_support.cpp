/*
 * host_artifact_support.cpp —— choreography 测试的两个 C++ 辅助能力，只在 host 用：
 *
 *  1) host_patch_artifact_single_step():
 *     现存 fixtures 是过期的整段图（runtime.timesteps=8/sync_steps=8），而当前 SNN Head
 *     各层契约要求单步图 timesteps==1 && sync_steps==1。本函数在内存里把已加载的
 *     flatbuffer 就地改成 timesteps=1、sync_steps=1（tick_depth 本就是 1），让真实层
 *     函数能通过 validate 并端到端跑起来，从而在 ASan/UBSan 下压测真实的强转/复用逻辑。
 *     只改标量字段值、不动任何 offset/结构，改后 verifier 仍通过。
 *
 *  2) host_enum_output_axons():
 *     枚举某层 output mapping 的 (elem_idx -> axon_bit_idx)。哨兵回显需要据此构造能被
 *     decode 命中的输出帧：DATA 用 axon_bit_idx 直接命中元素；VOLTAGE 的 axon_bit_idx
 *     即该元素 lane-0 的 base，四个 lane 为 base + lane*8。
 *
 * 复用 artifact_reader 的导航路径（见 Lib/runtime/artifact_reader.cpp）：
 *   artifact.io_mapping -> threads()[i] -> runtime() / output_mappings()->items()[0]->entries()
 */
#include "artifact_reader.h"
#include "generated/compile_artifacts_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <cstdint>

namespace fbs = paibox::backendv2::generated::fbs;

extern "C" int host_patch_artifact_single_step(uint8_t *buf, unsigned size)
{
    rvrt_artifact_t artifact;
    if (rvrt_artifact_read(buf, size, &artifact) != RVRT_ARTIFACT_OK) {
        return 1;
    }
    const auto *io = static_cast<const fbs::IOMapping *>(artifact.io_mapping);
    if ((io == nullptr) || (io->threads() == nullptr)) {
        return 2;
    }
    for (const auto *thread : *io->threads()) {
        const auto *runtime = (thread != nullptr) ? thread->runtime() : nullptr;
        if (runtime == nullptr) {
            return 3;
        }
        /* RuntimeParams 私有继承 flatbuffers::Table（无额外成员、无虚表），地址一致，
         * reinterpret_cast 后即可用其 public SetField 就地改写标量字段。 */
        auto *table = reinterpret_cast<flatbuffers::Table *>(
            const_cast<fbs::RuntimeParams *>(runtime));
        /* 两个字段在原 fixture 均为 8（非默认），字段存在，SetField 就地覆写为 1。 */
        if (!table->SetField<uint32_t>(fbs::RuntimeParams::VT_TIMESTEPS, 1U,
                                       0U) ||
            !table->SetField<uint32_t>(fbs::RuntimeParams::VT_SYNC_STEPS, 1U,
                                       0U)) {
            return 4;
        }
    }
    return 0;
}

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
