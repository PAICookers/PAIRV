#include "relay_protocol.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static void put_be_u32(uint32_t value, uint8_t *bytes)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static uint32_t crc_frames(const uint32_t *high, const uint32_t *low,
                           size_t count)
{
    uint8_t bytes[RELAY_FRAME_BYTES];
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < count; ++i) {
        put_be_u32(high[i], bytes);
        put_be_u32(low[i], bytes + 4U);
        crc = relay_crc32_update(crc, bytes, RELAY_FRAME_BYTES);
    }
    return crc ^ 0xFFFFFFFFU;
}

static void test_crc(void)
{
    static const uint8_t vector[] = "123456789";
    assert((relay_crc32_update(0xFFFFFFFFU, vector, 9U) ^ 0xFFFFFFFFU) ==
           0xCBF43926U);

    const uint32_t high[] = {0x01020304U, 0x8899AABBU};
    const uint32_t low[] = {0x05060708U, 0xCCDDEEFFU};
    assert(crc_frames(high, low, 2U) == 0x0EB44015U);
}

static void test_setup_fields(void)
{
    relay_config_setup_t setup;
    const uint32_t crc = 0x0EB44015U;
    /* N=2, CRC=1, source=host, low-first=1, reserved=0. */
    assert(relay_decode_config_setup(0xF000002EU, crc, &setup) == RELAY_OK);
    assert(setup.frame_count == 2U);
    assert(setup.expected_crc == crc);
    assert(setup.crc_enabled);
    assert(setup.enable_host_config);
    assert(setup.low_first);

    assert(relay_decode_config_setup(0xF000002FU, crc, &setup) ==
           RELAY_INVALID_FIELD);
    assert(relay_decode_config_setup(0xF0000020U, 1U, &setup) ==
           RELAY_INVALID_FIELD);
    assert(relay_decode_config_setup(0xF0000000U, 0U, &setup) ==
           RELAY_INVALID_FIELD);
    assert(relay_decode_config_setup(0xF100002EU, crc, &setup) ==
           RELAY_INVALID_FIELD);
    assert(relay_decode_config_setup(0xF000002EU, crc, NULL) ==
           RELAY_INVALID_FIELD);
}

static void test_source_and_word_order(void)
{
    relay_config_setup_t setup;

    /* Dynamic mode declaration: source bit must be set. */
    assert(relay_decode_config_setup(0xF0000014U, 0U, &setup) == RELAY_OK);
    assert(setup.enable_host_config);
    assert(!setup.low_first);

    /* Static mode declaration: source bit is clear and count is still set. */
    assert(relay_decode_config_setup(0xF0000010U, 0U, &setup) == RELAY_OK);
    assert(!setup.enable_host_config);
    assert(!setup.low_first);

    /* Application word order is represented by the low-first declaration. */
    assert(relay_decode_config_setup(0xF0000016U, 0U, &setup) == RELAY_OK);
    assert(setup.enable_host_config && setup.low_first);
}

static void test_ack_and_status_prefixes(void)
{
    uint32_t high = 0U;
    uint32_t low = 0U;
    relay_build_config_ack(RELAY_OK, 0x12345U, 0xAABBCCDDU, &high, &low);
    assert(high == 0xF1012345U);
    assert(low == 0xAABBCCDDU);

    relay_build_config_ack(RELAY_CRC_MISMATCH, 0x12345U, 0U, &high, &low);
    assert(high == 0xF1412345U);
    assert(low == 0U);

    relay_build_status(RELAY_NOC_READ_FAILED, 0x000023456789ABCDULL, &high,
                       &low);
    assert(high == 0xF2062345U);
    assert(low == 0x6789ABCDU);
}

int main(void)
{
    test_crc();
    test_setup_fields();
    test_source_and_word_order();
    test_ack_and_status_prefixes();
    puts("relay protocol host tests passed");
    return 0;
}
