#ifndef RVRT_MANAGED_PACKET_H
#define RVRT_MANAGED_PACKET_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RVRT_PACKET_MAGIC_SIZE 4U
#define RVRT_PACKET_HEADER_SIZE 16U
#define RVRT_PACKET_VERSION 1U

typedef enum rvrt_packet_command_e {
    RVRT_PACKET_COMMAND_HELLO = 1U,
    RVRT_PACKET_COMMAND_RUN_SAMPLE = 2U,
} rvrt_packet_command_t;

typedef enum rvrt_packet_status_e {
    RVRT_PACKET_STATUS_OK = 0U,
    RVRT_PACKET_STATUS_BAD_COMMAND = 1U,
    RVRT_PACKET_STATUS_BAD_LENGTH = 2U,
    RVRT_PACKET_STATUS_BAD_CRC = 3U,
    RVRT_PACKET_STATUS_ARTIFACT_ERROR = 4U,
    RVRT_PACKET_STATUS_RUNTIME_ERROR = 5U,
    RVRT_PACKET_STATUS_BUFFER_TOO_SMALL = 6U,
} rvrt_packet_status_t;

typedef struct rvrt_packet_header_s {
    uint8_t command;
    uint8_t status;
    uint32_t payload_len;
    uint32_t crc32;
} rvrt_packet_header_t;

uint32_t rvrt_packet_crc32(const uint8_t *data, uint32_t size);

void rvrt_packet_write_u32_le(uint8_t *dst, uint32_t value);

uint32_t rvrt_packet_read_u32_le(const uint8_t *src);

void rvrt_packet_build_header(uint8_t *dst, uint8_t command, uint8_t status,
                              const uint8_t *payload, uint32_t payload_len);

bool rvrt_packet_parse_header(const uint8_t *src, rvrt_packet_header_t *header);

bool rvrt_packet_validate_crc(const rvrt_packet_header_t *header,
                              const uint8_t *payload);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_MANAGED_PACKET_H */
