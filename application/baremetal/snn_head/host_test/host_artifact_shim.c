/*
 * host_artifact_shim.c —— 在 host 上提供各层 PAICore artifact 的 _start 符号，并在
 * 运行时把 fixtures/<layer>/compile_artifacts.bin 读入对应存储。
 *
 * 目标板上 snn_head_<layer>_artifact_start[] / _size[] 是 objcopy 生成的符号：
 *   - _start 的"地址"= 嵌入二进制首字节；
 *   - _size  的"地址"= 二进制字节数（并非可解引用的数组，仅取地址当整数）。
 * host 上：
 *   - _start：本文件直接以精确字节数定义同名数组（放 .bss、可写，供运行时 fread 填充）。
 *             注意：头文件把它声明为 const，本文件故意不 include 头文件、定义成非 const，
 *             以便写入；层文件仍按 const 只读引用，符号名一致、链接无碍。
 *   - _size ：由 CMake 的 -Wl,--defsym,<sym>=<字节数> 提供绝对符号（配合 -no-pie）。
 *
 * fc3 fixture 暂缺：给 1 字节占位数组，仅为链接通过（driver 不会调用 fc3）。
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* 直接定义与 objcopy 同名的 _start 数组（非 const，可写）。aligned 供 FlatBuffers
 * artifact 解析对齐。大小 == 对应 .bin 精确字节数（与 CMake 的 --defsym 一致）。 */
uint8_t snn_head_fc1_lif_artifact_start[1301844] __attribute__((aligned(16)));
uint8_t snn_head_block0_lif_artifact_start[2535884] __attribute__((aligned(16)));
uint8_t snn_head_block1_lif_artifact_start[2535884] __attribute__((aligned(16)));
uint8_t snn_head_fc2_artifact_start[2536040] __attribute__((aligned(16)));
uint8_t snn_head_fc3_artifact_start[1] __attribute__((aligned(16))); /* 占位 */

static int load_one(const char *dir, const char *sub, uint8_t *store,
                    size_t bytes)
{
    char path[1024];
    const int n = snprintf(path, sizeof(path), "%s/%s/compile_artifacts.bin",
                           dir, sub);
    if ((n < 0) || ((size_t)n >= sizeof(path))) {
        return 1;
    }
    FILE *const f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    const size_t got = fread(store, 1U, bytes, f);
    fclose(f);
    if (got != bytes) {
        fprintf(stderr, "artifact size mismatch: %s got=%zu want=%zu\n", path,
                got, bytes);
        return 1;
    }
    return 0;
}

/* 把 4 个有 fixture 的层 artifact 读进各自存储；返回 0 成功。 */
int snn_head_host_load_artifacts(const char *fixture_dir)
{
    if (load_one(fixture_dir, "fc1_lif", snn_head_fc1_lif_artifact_start,
                 1301844U) != 0) {
        return 1;
    }
    if (load_one(fixture_dir, "block0_lif", snn_head_block0_lif_artifact_start,
                 2535884U) != 0) {
        return 1;
    }
    if (load_one(fixture_dir, "block1_lif", snn_head_block1_lif_artifact_start,
                 2535884U) != 0) {
        return 1;
    }
    if (load_one(fixture_dir, "fc2", snn_head_fc2_artifact_start, 2536040U) !=
        0) {
        return 1;
    }
    return 0;
}
