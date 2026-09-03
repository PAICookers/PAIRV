#include "utils.h"

#include <errno.h>
#include <stdlib.h>

/* Parse a right-aligned binary literal while accepting digit separators. */
static int parse_binary(const char *text, uint32_t *high, uint32_t *low)
{
    uint64_t value = 0U;
    unsigned digits = 0U;
    int previous_digit = 0;

    if (text[0] == '\0') {
        return -1;
    }
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == '0' || *cursor == '1') {
            if (digits == 64U) {
                return -1;
            }
            value = (value << 1U) | (uint64_t)(*cursor - '0');
            ++digits;
            previous_digit = 1;
        } else if (*cursor == '_') {
            if (!previous_digit || cursor[1] == '\0' ||
                (cursor[1] != '0' && cursor[1] != '1')) {
                return -1;
            }
            previous_digit = 0;
        } else {
            return -1;
        }
    }
    if (digits == 0U) {
        return -1;
    }
    *high = (uint32_t)(value >> 32U);
    *low = (uint32_t)value;
    return 0;
}

int uart_app_parse_u64(const char *text, uint32_t *high, uint32_t *low)
{
    char *end = NULL;
    unsigned long long parsed;
    int base = 10;

    if (text == NULL || high == NULL || low == NULL || text[0] == '\0' ||
        text[0] == '+' || text[0] == '-') {
        return -1;
    }
    if (text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
        return parse_binary(text + 2, high, low);
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        if (text[2] == '\0') {
            return -1;
        }
    }
    errno = 0;
    parsed = strtoull(text, &end, base);
    if (errno == ERANGE || end == text || *end != '\0') {
        return -1;
    }
    *high = (uint32_t)(parsed >> 32U);
    *low = (uint32_t)parsed;
    return 0;
}

user_frame_kind_t uart_app_classify_frame(uint32_t high, uint32_t low,
                                          uint32_t *expected_frames)
{
    const uint32_t type = (high >> 30U) & 0x3U;
    if (expected_frames != NULL) {
        *expected_frames = 0U;
    }
    switch (type) {
        case 0U:
            if (expected_frames != NULL) {
                *expected_frames = 1U;
            }
            return USER_CONFIG;
        case 1U:
            if (expected_frames != NULL) {
                *expected_frames = (low & 0x3FFFU) + 1U;
            }
            return USER_TEST;
        case 2U:
            return USER_WORK;
        default:
            return USER_CONTROL;
    }
}

const char *uart_app_frame_name(frame_kind_t kind)
{
    static const char *const names[FRAME_KIND_COUNT] = {"config", "init",
                                                        "work", "sync", "test"};
    return kind < FRAME_KIND_COUNT ? names[kind] : "unknown";
}

const char *uart_app_frame_status_name(frame_status_t status)
{
    switch (status) {
        case FRAME_AVAILABLE:
            return "available";
        case FRAME_UNAVAILABLE:
            return "unavailable";
        default:
            return "invalid";
    }
}
