#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ReadBufferFixedSize.h"
#include "rv_debug.h"
#include "test.h"

extern "C"
{
    extern const uint8_t _binary_proto_test_message_pb_start[];
    extern const uint8_t _binary_proto_test_message_pb_size[];
}

using FlashPbMessage = TestMsg<
    12, // meta.build_id
    2,  // input_shape repeated length
    2,  // outputs repeated length
    8,  // outputs.debug_name
    2,  // outputs.shape repeated length
    3   // outputs.route_spans repeated length
    >;

namespace
{
    constexpr uint32_t kReadBufferCapacity = 128U;
    constexpr const char* kAppTitle = "proto_flash";
}

int main()
{
    rv_debug_set_level(RV_DEBUG_INFO);

    const uint8_t *const pb_data = _binary_proto_test_message_pb_start;
    const auto pb_size = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_binary_proto_test_message_pb_size));

    if (pb_size == 0U)
    {
        RV_DEBUG_LOGE(kAppTitle, "pb asset is empty");
        std::printf("pb asset is empty\r\n");
        return 1;
    }

    if (pb_size > kReadBufferCapacity)
    {
        RV_DEBUG_LOGE(
            kAppTitle,
            "pb asset exceeds read buffer: %u",
            static_cast<unsigned>(pb_size));
        std::printf("pb asset exceeds read buffer: %u\r\n", static_cast<unsigned>(pb_size));
        return 2;
    }

    EmbeddedProto::ReadBufferFixedSize<kReadBufferCapacity> read_buffer;
    std::memcpy(read_buffer.get_data(), pb_data, pb_size);
    read_buffer.set_bytes_written(pb_size);

    FlashPbMessage message;
    auto error = message.deserialize(read_buffer);
    if (error != EmbeddedProto::Error::NO_ERRORS)
    {
        RV_DEBUG_LOGE(
            kAppTitle,
            "deserialize failed, error=%d",
            static_cast<int>(error));
        std::printf("deserialize failed, error=%d\r\n", static_cast<int>(error));
        return 3;
    }

    const auto &meta = message.get_meta();
    const auto &outputs = message.get_outputs();
    if ((meta.get_schema_version() != 2U) ||
        (std::strcmp(meta.get_build_id().get_const(), "flash-smoke") != 0) ||
        (meta.get_codec() != OutputCodec::DIRECT_AXON) ||
        (outputs.get_length() != 2U) ||
        (outputs[0].get_route_spans().get_length() != 3U))
    {
        RV_DEBUG_LOGE(kAppTitle, "payload mismatch");
        std::printf("payload mismatch\r\n");
        return 4;
    }

    if ((message.get_enabled() != true) || (message.get_offset() != -12))
    {
        RV_DEBUG_LOGE(kAppTitle, "scalar mismatch");
        std::printf("scalar mismatch\r\n");
        return 5;
    }

    RV_DEBUG_LOGI(
        kAppTitle,
        "pb bytes=%u outputs=%u",
        static_cast<unsigned>(pb_size),
        static_cast<unsigned>(outputs.get_length()));

    std::printf("pb bytes: %u\r\n", static_cast<unsigned>(pb_size));
    std::printf("outputs: %u\r\n", static_cast<unsigned>(outputs.get_length()));
    std::printf("EmbeddedProto flash pb test passed\r\n");
    return 0;
}
