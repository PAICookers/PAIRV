#ifndef RVRT_MANAGED_PACKET_H
#define RVRT_MANAGED_PACKET_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of magic bytes in a managed packet header. */
#define RVRT_PACKET_MAGIC_SIZE 4U
/** Serialized header size: magic/version/command/status/length/CRC. */
#define RVRT_PACKET_HEADER_SIZE 16U
/** Managed UART protocol version accepted by this runtime. */
#define RVRT_PACKET_VERSION 1U

/** @brief Commands accepted by the managed UART runtime protocol. */
typedef enum rvrt_packet_command_e {
    /** Request protocol identification. */
    RVRT_PACKET_COMMAND_HELLO = 1U,
    /** Submit one managed inference sample. */
    RVRT_PACKET_COMMAND_RUN_SAMPLE = 2U,
} rvrt_packet_command_t;

/** @brief Status byte carried by a managed UART response packet. */
typedef enum rvrt_packet_status_e {
    /** Request completed successfully. */
    RVRT_PACKET_STATUS_OK = 0U,
    /** Command byte is unsupported. */
    RVRT_PACKET_STATUS_BAD_COMMAND = 1U,
    /** Header or payload length is invalid. */
    RVRT_PACKET_STATUS_BAD_LENGTH = 2U,
    /** Payload checksum does not match. */
    RVRT_PACKET_STATUS_BAD_CRC = 3U,
    /** Artifact verification/access failed. */
    RVRT_PACKET_STATUS_ARTIFACT_ERROR = 4U,
    /** PAICORE runtime operation failed. */
    RVRT_PACKET_STATUS_RUNTIME_ERROR = 5U,
    /** Device storage is insufficient. */
    RVRT_PACKET_STATUS_BUFFER_TOO_SMALL = 6U,
} rvrt_packet_status_t;

/**
 * @brief Parsed managed packet header; payload bytes are external.
 *
 * The 16-byte wire header is `RVRT`, version, command, status, reserved byte,
 * little-endian payload_len, then little-endian CRC-32. This structure is a
 * decoded host-endian view and must not be cast over wire bytes.
 */
typedef struct rvrt_packet_header_s {
    /** Command byte at wire offset 5. */
    uint8_t command;
    /** Response status byte at wire offset 6. */
    uint8_t status;
    /** Little-endian wire payload length, converted to host order. */
    uint32_t payload_len;
    /** Little-endian wire CRC-32, converted to host order. */
    uint32_t crc32;
} rvrt_packet_header_t;

/**
 * @brief Compute the protocol's reflected IEEE CRC-32 for arbitrary bytes.
 *
 * Uses polynomial 0xEDB88320 with initial and final XOR values of
 * 0xFFFFFFFF. data may be NULL only when size is zero.
 * @param data Bytes to hash, or NULL when size is zero.
 * @param size Number of bytes in data.
 * @return CRC-32 value in host integer order.
 */
uint32_t rvrt_packet_crc32(const uint8_t *data, uint32_t size);

/**
 * @brief Store one 32-bit unsigned value in little-endian byte order.
 * @param dst Destination byte array with at least four bytes.
 * @param value Value to encode.
 * @note NULL dst is a no-op because this low-level convenience API has no
 *       status return.
 */
void rvrt_packet_write_u32_le(uint8_t *dst, uint32_t value);

/**
 * @brief Build a fixed-size managed header and its payload CRC.
 *
 * Writes magic, version, command, status, reserved byte, payload length, and
 * CRC. The CRC covers header bytes [0, 12) followed by payload bytes; payload
 * storage itself is not copied.
 * @param dst Header buffer with at least RVRT_PACKET_HEADER_SIZE bytes.
 * @param dst_size Available bytes in dst.
 * @param command Wire command byte.
 * @param status Wire response-status byte.
 * @param payload Payload bytes, or NULL when payload_len is zero.
 * @param payload_len Payload length in bytes.
 * @return true on success; false for insufficient header storage or an invalid
 *         NULL payload/nonzero length pair.
 */
bool rvrt_packet_build_header(uint8_t *dst, uint32_t dst_size, uint8_t command,
                              uint8_t status, const uint8_t *payload,
                              uint32_t payload_len);

/**
 * @brief Validate and decode a fixed-size managed packet header.
 *
 * Checks magic, protocol version, reserved byte, and header capacity. It does
 * not validate command/status values or the payload CRC; call
 * rvrt_packet_validate_crc() after obtaining the full payload.
 * @param src Header bytes received from the transport.
 * @param src_size Available bytes in src.
 * @param header Receives decoded host-endian wire metadata.
 * @return true only when the fixed header is structurally valid.
 */
bool rvrt_packet_parse_header(const uint8_t *src, uint32_t src_size,
                              rvrt_packet_header_t *header);

/**
 * @brief Validate that payload size and bytes match a parsed header CRC.
 * @param header Parsed header carrying the expected payload length and CRC.
 * @param payload Payload bytes, or NULL when payload_size is zero.
 * @param payload_size Actual number of supplied payload bytes.
 * @return true only when payload_size equals header payload_len and the
 *         protocol CRC matches.
 */
bool rvrt_packet_validate_crc(const rvrt_packet_header_t *header,
                              const uint8_t *payload, uint32_t payload_size);

#ifdef __cplusplus
}
#endif

#endif /* RVRT_MANAGED_PACKET_H */
