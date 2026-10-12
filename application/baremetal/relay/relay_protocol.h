#ifndef RELAY_PROTOCOL_H
#define RELAY_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENABLE_HOST_CONFIG
#define ENABLE_HOST_CONFIG 0
#endif

#ifndef RELAY_HOST_HIGH_FIRST
#define RELAY_HOST_HIGH_FIRST 1
#endif

#define RELAY_FRAME_BYTES 8U
#define RELAY_CONFIG_COUNT_MAX 0xFFFFFU

/* 8-bit Host/relay opcodes start at 0xF0; offline control headers are 0xC/D/E.
 * 0xF* is NOT globally unused: online CTRL_TYPE4 has 4-bit header 0xF too.
 * Parse SETUP only in WAIT_SETUP; config payload/application frames stay
 * opaque. */
typedef enum {
    RELAY_CONFIG_SETUP = 0xF0,
    RELAY_CONFIG_ACK = 0xF1,
    RELAY_STATUS = 0xF2,
} relay_opcode_t;

typedef enum {
    RELAY_OK = 0,
    RELAY_MODE_MISMATCH = 1,
    RELAY_INVALID_FIELD = 2,
    RELAY_COUNT_MISMATCH = 3,
    RELAY_CRC_MISMATCH = 4,
    RELAY_UART_RX_OVERFLOW = 5,
    RELAY_NOC_READ_FAILED = 6,
    RELAY_UART_TX_FAILED = 7,
    RELAY_UNEXPECTED_RESPONSE = 8,
    RELAY_INVALID_SYNC = 9,
    RELAY_INVALID_STATE = 10,
    RELAY_INIT_FAILED = 11,
    RELAY_UNSUPPORTED_MESSAGE = 12,
} relay_error_t;

/* SETUP declares source, frame count, CRC and application word order. */
typedef struct {
    uint32_t frame_count;
    uint32_t expected_crc;
    bool crc_enabled;
    bool enable_host_config;
    bool low_first;
} relay_config_setup_t;

/* high/low are logical SETUP words; write setup only on RELAY_OK.
 * Validates wire fields; main checks source/order against the firmware build.
 */
relay_error_t relay_decode_config_setup(uint32_t high, uint32_t low,
                                        relay_config_setup_t *setup);

/* error is the result, forwarded_count counts NoC writes, crc is finalized
 * (zero if disabled); high/low receive logical ACK words, sent high-first. */
void relay_build_config_ack(relay_error_t error, uint32_t forwarded_count,
                            uint32_t crc, uint32_t *high, uint32_t *low);
/* error is nonzero, detail is 48-bit evidence; high/low receive logical words.
 * Caller chooses high-first during config, application order after ACK. */
void relay_build_status(relay_error_t error, uint64_t detail, uint32_t *high,
                        uint32_t *low);

/* Update running CRC with count bytes; seed/final XOR are both 0xFFFFFFFF. */
uint32_t relay_crc32_update(uint32_t crc, const uint8_t *bytes, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_PROTOCOL_H */
