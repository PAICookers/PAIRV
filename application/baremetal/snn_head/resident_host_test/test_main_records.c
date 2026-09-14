#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

size_t snn_head_test_format_resident_init(char *buffer, size_t capacity,
                                          const char *status, uint64_t cycles,
                                          uint32_t hz, uint32_t config_frames);
size_t snn_head_test_format_residency_audit(
    char *buffer, size_t capacity, const char *phase, const char *status,
    uint64_t cycles, uint32_t windows, uint64_t bytes, uint32_t layer,
    uint32_t query, uint32_t frame, uint32_t expected_high,
    uint32_t expected_low, uint32_t actual_high, uint32_t actual_low);

static void test_init_record_uses_full_decimal_u64(void)
{
    char line[192];
    const size_t length = snn_head_test_format_resident_init(
        line, sizeof(line), "ok", UINT64_MAX, 408000000U, 143U);
    static const char expected[] =
        "SNN_HEAD_RESIDENT_INIT status=ok cycles=18446744073709551615 "
        "hz=408000000 config_frames=143\r\n";
    assert(length == sizeof(expected) - 1U);
    assert(strcmp(line, expected) == 0);
}

static void test_audit_record_preserves_decimal_and_hex_fields(void)
{
    char line[320];
    const size_t length = snn_head_test_format_residency_audit(
        line, sizeof(line), "before_init", "ok", UINT64_MAX, 4294967295U,
        UINT64_C(18446744073709551615), 4U, 11U, 143U, 0x01234567U, 0x89abcdefU,
        0xfedcba98U, 0x76543210U);
    static const char expected[] =
        "SNN_HEAD_RESIDENT_AUDIT phase=before_init status=ok "
        "cycles=18446744073709551615 windows=4294967295 "
        "bytes=18446744073709551615 layer=4 query=11 frame=143 "
        "expected=0123456789abcdef actual=fedcba9876543210\r\n";
    assert(length == sizeof(expected) - 1U);
    assert(strcmp(line, expected) == 0);
}

static void test_record_writer_rejects_truncation(void)
{
    char line[32];
    assert(snn_head_test_format_resident_init(line, sizeof(line), "ok",
                                              UINT64_MAX, UINT32_MAX,
                                              UINT32_MAX) == 0U);
}

static void write_record(const char *line, size_t length)
{
    assert(length != 0U);
    assert(fwrite(line, 1U, length, stdout) == length);
}

static void emit_validator_fixture(void)
{
    char line[320];
    size_t length = snn_head_test_format_resident_init(
        line, sizeof(line), "ok", UINT64_C(21012892861), 408000000U, 143U);
    write_record(line, length);

    static const char audit_plan[] =
        "SNN_HEAD_RESIDENT_AUDIT_PLAN "
        "sha256="
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\n";
    write_record(audit_plan, sizeof(audit_plan) - 1U);

    length = snn_head_test_format_residency_audit(
        line, sizeof(line), "before_init", "ok", UINT64_C(8468552), 8450U,
        UINT64_C(8468552), 4U, 11U, 143U, 0x00000004U, 0x0000000bU, 0x00000004U,
        0x0000000bU);
    write_record(line, length);

    static const char ready[] = "SNN_HEAD_UART_READY\r\n";
    write_record(ready, sizeof(ready) - 1U);

    length = snn_head_test_format_residency_audit(
        line, sizeof(line), "after_chunk_weights", "ok", UINT64_C(8268288),
        8158U, UINT64_C(8268288), 4U, 10U, 128U, 0x00000004U, 0x0000000aU,
        0x00000004U, 0x0000000aU);
    write_record(line, length);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--records") == 0) {
        emit_validator_fixture();
        return 0;
    }
    test_init_record_uses_full_decimal_u64();
    test_audit_record_preserves_decimal_and_hex_fields();
    test_record_writer_rejects_truncation();
    puts("snn_head_main_records: 3 tests passed");
    return 0;
}
