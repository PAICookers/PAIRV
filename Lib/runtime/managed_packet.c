#include "managed_packet.h"

#include <stddef.h>

#define RVRT_PACKET_CRC32_POLY 0xEDB88320UL
#define RVRT_PACKET_CRC32_INIT 0xFFFFFFFFUL
#define RVRT_PACKET_MAGIC0 ((uint8_t)'R')
#define RVRT_PACKET_MAGIC1 ((uint8_t)'V')
#define RVRT_PACKET_MAGIC2 ((uint8_t)'R')
#define RVRT_PACKET_MAGIC3 ((uint8_t)'T')
#define RVRT_PACKET_RESERVED 0U

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    if ((data == NULL) && (size != 0U)) {
        return crc;
    }

    for (uint32_t i = 0U; i < size; ++i) {
        crc ^= (uint32_t)data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (RVRT_PACKET_CRC32_POLY & mask);
        }
    }
    return crc;
}

static uint32_t crc32_finish(uint32_t crc) { return crc ^ 0xFFFFFFFFUL; }

uint32_t rvrt_packet_crc32(const uint8_t *data, uint32_t size)
{
    return crc32_finish(crc32_update(RVRT_PACKET_CRC32_INIT, data, size));
}

void rvrt_packet_write_u32_le(uint8_t *dst, uint32_t value)
{
    if (dst == NULL) {
        return;
    }
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16U) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static uint32_t rvrt_packet_read_u32_le(const uint8_t *src)
{
    if (src == NULL) {
        return 0U;
    }
    return ((uint32_t)src[0]) | (((uint32_t)src[1]) << 8U) |
           (((uint32_t)src[2]) << 16U) | (((uint32_t)src[3]) << 24U);
}

static void build_header_prefix(uint8_t *dst, uint8_t command, uint8_t status,
                                uint32_t payload_len)
{
    dst[0] = RVRT_PACKET_MAGIC0;
    dst[1] = RVRT_PACKET_MAGIC1;
    dst[2] = RVRT_PACKET_MAGIC2;
    dst[3] = RVRT_PACKET_MAGIC3;
    dst[4] = RVRT_PACKET_VERSION;
    dst[5] = command;
    dst[6] = status;
    dst[7] = RVRT_PACKET_RESERVED;
    rvrt_packet_write_u32_le(dst + 8U, payload_len);
}

static uint32_t compute_crc(uint8_t command, uint8_t status,
                            const uint8_t *payload, uint32_t payload_len)
{
    uint8_t prefix[RVRT_PACKET_HEADER_SIZE - 4U];
    build_header_prefix(prefix, command, status, payload_len);

    uint32_t crc = RVRT_PACKET_CRC32_INIT;
    crc = crc32_update(crc, prefix, (uint32_t)sizeof(prefix));
    crc = crc32_update(crc, payload, payload_len);
    return crc32_finish(crc);
}

bool rvrt_packet_build_header(uint8_t *dst, uint32_t dst_size, uint8_t command,
                              uint8_t status, const uint8_t *payload,
                              uint32_t payload_len)
{
    if ((dst == NULL) || (dst_size < RVRT_PACKET_HEADER_SIZE) ||
        ((payload == NULL) && (payload_len != 0U))) {
        return false;
    }
    build_header_prefix(dst, command, status, payload_len);
    rvrt_packet_write_u32_le(
        dst + 12U, compute_crc(command, status, payload, payload_len));
    return true;
}

bool rvrt_packet_parse_header(const uint8_t *src, uint32_t src_size,
                              rvrt_packet_header_t *header)
{
    if ((src == NULL) || (src_size < RVRT_PACKET_HEADER_SIZE) ||
        (header == NULL)) {
        return false;
    }
    if ((src[0] != RVRT_PACKET_MAGIC0) || (src[1] != RVRT_PACKET_MAGIC1) ||
        (src[2] != RVRT_PACKET_MAGIC2) || (src[3] != RVRT_PACKET_MAGIC3) ||
        (src[4] != RVRT_PACKET_VERSION) || (src[7] != RVRT_PACKET_RESERVED)) {
        return false;
    }

    header->command = src[5];
    header->status = src[6];
    header->payload_len = rvrt_packet_read_u32_le(src + 8U);
    header->crc32 = rvrt_packet_read_u32_le(src + 12U);
    return true;
}

bool rvrt_packet_validate_crc(const rvrt_packet_header_t *header,
                              const uint8_t *payload, uint32_t payload_size)
{
    if ((header == NULL) || (payload_size != header->payload_len) ||
        ((payload == NULL) && (payload_size != 0U))) {
        return false;
    }
    const uint32_t expected = compute_crc(header->command, header->status,
                                          payload, header->payload_len);
    return header->crc32 == expected;
}
