/*
 * 逐层中间结果 dump sink。
 *
 * 仅在 -DSNN_HEAD_ENABLE_DUMP（board_test 用 make DUMP=1）时编入并生效；未开启时
 * 整个文件为空翻译单元，对普通测试与部署固件零影响。
 *
 * 输出格式：一行一个元素，机器可解析，便于和 Python 参考逐元素 diff。
 *   float32：`<idx> 0x<IEEE754位> <round(v*1e6)>`（hex 位是 bit 精确值）
 *   int8/int32/uint8：`<idx> <十进制值>`
 * 每段以 `>>> DUMP <tag> count=.. dtype=..` 开头、`<<< END <tag>` 结尾。
 */
#ifdef SNN_HEAD_ENABLE_DUMP

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "snn_head_internal.h"

void snn_head_dump_stage(const char *tag, const void *data, uint32_t count,
                         snn_head_dump_dtype_t dtype)
{
    printf(">>> DUMP %s count=%u dtype=%d\r\n", tag, (unsigned)count, (int)dtype);

    for (uint32_t i = 0U; i < count; ++i) {
        switch (dtype) {
        case SNN_HEAD_DUMP_F32: {
            const float v = ((const float *)data)[i];
            uint32_t bits = 0U;
            memcpy(&bits, &v, sizeof(bits));
            const long micro =
                (long)((double)v * 1000000.0 + ((v >= 0.0f) ? 0.5 : -0.5));
            printf("%u 0x%08lx %ld\r\n", (unsigned)i, (unsigned long)bits, micro);
            break;
        }
        case SNN_HEAD_DUMP_S8:
            printf("%u %d\r\n", (unsigned)i, (int)((const int8_t *)data)[i]);
            break;
        case SNN_HEAD_DUMP_S32:
            printf("%u %ld\r\n", (unsigned)i, (long)((const int32_t *)data)[i]);
            break;
        case SNN_HEAD_DUMP_U8:
            printf("%u %u\r\n", (unsigned)i, (unsigned)((const uint8_t *)data)[i]);
            break;
        default:
            break;
        }
    }

    printf("<<< END %s\r\n", tag);
}

#endif /* SNN_HEAD_ENABLE_DUMP */
