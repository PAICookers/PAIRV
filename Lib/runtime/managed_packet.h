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

/** @brief Commands accepted by the managed UART runtime protocol. */
typedef enum rvrt_packet_command_e {
    RVRT_PACKET_COMMAND_HELLO = 1U,
    RVRT_PACKET_COMMAND_RUN_SAMPLE = 2U,
} rvrt_packet_command_t;

/** @brief Status byte carried by a managed UART response packet. */
typedef enum rvrt_packet_status_e {
    RVRT_PACKET_STATUS_OK = 0U,
    RVRT_PACKET_STATUS_BAD_COMMAND = 1U,
    RVRT_PACKET_STATUS_BAD_LENGTH = 2U,
    RVRT_PACKET_STATUS_BAD_CRC = 3U,
    RVRT_PACKET_STATUS_ARTIFACT_ERROR = 4U,
    RVRT_PACKET_STATUS_RUNTIME_ERROR = 5U,
    RVRT_PACKET_STATUS_BUFFER_TOO_SMALL = 6U,
} rvrt_packet_status_t;

/** @brief Parsed managed packet header; payload bytes are external. */
typedef struct rvrt_packet_header_s {
    uint8_t command;
    uint8_t status;
    uint32_t payload_len;
    uint32_t crc32;
} rvrt_packet_header_t;

/**
 * @brief Compute CRC-32; data may be NULL only when size is zero.
 * @param data Payload bytes to hash.
 * @param size Number of bytes in data.
 */
uint32_t rvrt_packet_crc32(const uint8_t *data, uint32_t size);

/**
 * @brief Store one 32-bit unsigned value in little-endian byte order.
 * @param dst Destination byte array with at least four bytes.
 * @param value Value to encode.
 */
void rvrt_packet_write_u32_le(uint8_t *dst, uint32_t value);

/**
 * @brief Build a fixed-size header and CRC for a caller-owned payload.
 * @param dst Header buffer with at least RVRT_PACKET_HEADER_SIZE bytes.
 * @param dst_size Available bytes in dst.
 * @param command Wire command byte.
 * @param status Wire response-status byte.
 * @param payload Payload bytes, or NULL when payload_len is zero.
 * @param payload_len Payload length in bytes.
 */
bool rvrt_packet_build_header(uint8_t *dst, uint32_t dst_size, uint8_t command,
                              uint8_t status, const uint8_t *payload,
                              uint32_t payload_len);

/**
 * @brief Validate and decode a fixed-size managed packet header.
 * @param src Header bytes received from the transport.
 * @param src_size Available bytes in src.
 * @param header Receives decoded wire metadata.
 */
bool rvrt_packet_parse_header(const uint8_t *src, uint32_t src_size,
                              rvrt_packet_header_t *header);

/**
 * @brief Validate that payload size and bytes match a parsed header CRC.
 * @param header Parsed header carrying the expected payload length and CRC.
 * @param payload Payload bytes, or NULL when payload_size is zero.
 * @param payload_size Actual number of supplied payload bytes.
 */
bool rvrt_packet_validate_crc(const rvrt_packet_header_t *header,
                              const uint8_t *payload, uint32_t payload_size);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_MANAGED_PACKET_H */
