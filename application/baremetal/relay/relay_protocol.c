#include "relay_protocol.h"

static uint32_t encode_opcode(uint8_t opcode, uint8_t error)
{
    return ((uint32_t)opcode << 24U) | ((uint32_t)error << 16U);
}

relay_error_t relay_decode_config_setup(uint32_t high, uint32_t low,
                                        relay_config_setup_t *setup)
{
    if (setup == 0 || (high >> 24U) != RELAY_CONFIG_SETUP) {
        return RELAY_INVALID_FIELD;
    }

    const uint32_t fields = high & 0x00FFFFFFU;
    /* SETUP packs count[23:4], CRC[3], source[2], order[1], reserved[0]. */
    const uint32_t frame_count = (fields >> 4U) & RELAY_CONFIG_COUNT_MAX;
    const bool crc_enabled = (fields & (1U << 3U)) != 0U;
    const bool enable_host_config = (fields & (1U << 2U)) != 0U;
    const bool low_first = (fields & (1U << 1U)) != 0U;
    if ((fields & 1U) != 0U || frame_count == 0U ||
        (!crc_enabled && low != 0U)) {
        return RELAY_INVALID_FIELD;
    }

    setup->frame_count = frame_count;
    setup->expected_crc = low;
    setup->crc_enabled = crc_enabled;
    setup->enable_host_config = enable_host_config;
    setup->low_first = low_first;
    return RELAY_OK;
}

void relay_build_config_ack(relay_error_t error, uint32_t forwarded_count,
                            uint32_t crc, uint32_t *high, uint32_t *low)
{
    /* ACK packs a 4-bit error and the same 20-bit count; low holds CRC32. */
    *high = ((uint32_t)RELAY_CONFIG_ACK << 24U) |
            (((uint32_t)error & 0x0FU) << 20U) |
            (forwarded_count & RELAY_CONFIG_COUNT_MAX);
    *low = crc;
}

void relay_build_status(relay_error_t error, uint64_t detail, uint32_t *high,
                        uint32_t *low)
{
    *high = encode_opcode(RELAY_STATUS, (uint8_t)error) |
            (uint32_t)((detail >> 32U) & 0xFFFFU);
    *low = (uint32_t)detail;
}

uint32_t relay_crc32_update(uint32_t crc, const uint8_t *bytes, uint32_t count)
{
    static const uint32_t table[16] = {
        0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
        0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
        0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
        0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU,
    };
    for (uint32_t i = 0U; i < count; ++i) {
        crc ^= bytes[i];
        crc = (crc >> 4U) ^ table[crc & 0xFU];
        crc = (crc >> 4U) ^ table[crc & 0xFU];
    }
    return crc;
}
