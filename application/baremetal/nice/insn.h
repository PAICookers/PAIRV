#ifndef __INSN_H__
#define __INSN_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "nuclei_sdk_soc.h"

    /*
     ******************************************************************************************
     * NICE Extension Instruction Format:
     *  .insn and r indicates this is a pseudo and R-type instruction.
     *  0x5b is the value of the opcode field, which means it is a
     *  NICE instruction belonging to custom3.
     * Supported format: only R type here
     * This NICE Demo implements the following 3 instructions for NICE-Core:
     * * CLW or lbuf: Load 12-byte data from memory to row buffer.
     * * CSW or sbuf: Store 12-byte data from row buffer to memory.
     * * CACC or rowsum: Sums a row of the matrix, and columns are accumulated automatically.
     * Supported instructions for this nice demo:
     *  1. custom2 lbuf: burst 4 load(4 cycles) data in memory to row_buf
     *     lbuf (a1)
     *     .insn r opcode, func3, func7, rd, rs1, rs2
     *  2. custom2 sbuf: burst 4 store(4 cycles) row_buf to memory
     *     sbuf (a1)
     *     .insn r opcode, func3, func7, rd, rs1, rs2
     *  3. custom2 acc rowsum: load data from memory(@a1), accumulate row data and write back
     *     rowsum rd, a1, x0(N cycles)
     *     .insn r opcode, func3, func7, rd, rs1, rs2
     ******************************************************************************************
     */

#define ROW_LEN 3
#define COL_LEN 3

    // TODO: demo nice opcode change according to rtl updates:
    // demo nice is just an demo used to prove Nuclei NICE feature, so this opcode might change frequently
    // 20231023: 0x5b -> 0xb
    // 20220721: 0x7b -> 0x5b

    /** custom nice instruction setup */
    __STATIC_FORCEINLINE void custom_bf16_setup(uint16_t *base_addr, uint16_t *target_addr)
    {
        int zero = 0;

        asm volatile(".insn r 0xb, 3, 0, x0, %1, %2"
                     : "=r"(zero)
                     : "r"(base_addr), "r"(target_addr));
    }

    __STATIC_FORCEINLINE void custom_bf16_wsetup(unsigned int word_len)
    {
        int zero = 0;

        asm volatile(".insn r 0xb, 2, 0, x0, %1, x0"
                     : "=r"(zero)
                     : "r"(word_len));
    }

    /** custom load and acc */
    __STATIC_FORCEINLINE unsigned short custom_bf16_load_acc(uint16_t *base_addr)
    {
        unsigned short acc_res;

        asm volatile(".insn r 0xb, 6, 1, %0, %1, x0"
                     : "=r"(acc_res)
                     : "r"(base_addr));
        return acc_res;
    }

    /** custom div */
    __STATIC_FORCEINLINE unsigned short custom_bf16_div(unsigned short acc_res)
    {
        unsigned short div_res;

        asm volatile(".insn r 0xb, 6, 2, %0, %1, x0"
                     : "=r"(div_res)
                     : "r"(acc_res));
        return div_res;
    }

    __STATIC_FORCEINLINE unsigned short custom_bf16_square_sum(uint16_t *load_base_addr)
    {
        unsigned short square_sum_res;

        asm volatile(".insn r 0xb, 6, 3, %0, %1, x0"
                     : "=r"(square_sum_res)
                     : "r"(load_base_addr));
        return square_sum_res;
    }

    __STATIC_FORCEINLINE void custom_bf16_load_sub_store(unsigned short mean, uint16_t *store_base_addr)
    {
        int zero = 0;

        asm volatile(".insn r 0xb, 3, 4, x0, %1, %2"
                     : "=r"(zero)
                     : "r"(mean), "r"(store_base_addr));
    }

    __STATIC_FORCEINLINE unsigned short custom_bf16_sqrt(unsigned short mean)
    {
        unsigned short sqrt_mean = 0;

        asm volatile(".insn r 0xb, 6, 5, %0, %1, x0"
                     : "=r"(sqrt_mean)
                     : "r"(mean));
        return sqrt_mean;
    }

    __STATIC_FORCEINLINE void custom_bf16_load_div_store(uint16_t *load_base_addr, unsigned short sqrt_mean)
    {
        int zero = 0;

        asm volatile(".insn r 0xb, 3, 6, x0, %1, %2"
                     : "=r"(zero)
                     : "r"(load_base_addr), "r"(sqrt_mean));
    }

    __STATIC_FORCEINLINE void custom_bf16_load_exp_store(uint16_t *load_base_addr, uint16_t *store_base_addr)
    {
        int zero = 0;

        asm volatile(".insn r 0xb, 3, 7, x0, %1, %2"
                     : "=r"(zero)
                     : "r"(load_base_addr), "r"(store_base_addr));
    }

    /** normal test case without NICE accelerator. */
    void normal_case(unsigned int array[ROW_LEN][COL_LEN], unsigned int col_sum[COL_LEN], unsigned int row_sum[ROW_LEN]);

    /** teat case using NICE accelerator. */
    void nice_case(unsigned int array[ROW_LEN][COL_LEN], unsigned int col_sum[COL_LEN], unsigned int row_sum[ROW_LEN]);

    /** print input array */
    void print_array(unsigned int array[ROW_LEN][COL_LEN]);

    /** print matrix result */
    void print_result(unsigned int col_sum[COL_LEN], unsigned int row_sum[ROW_LEN]);

    /** compare result of reference and nice */
    int compare_result(unsigned int ref_cs[COL_LEN], unsigned int ref_rs[ROW_LEN],
                       unsigned int nice_cs[COL_LEN], unsigned int nice_rs[ROW_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* __INSN_H__ */
