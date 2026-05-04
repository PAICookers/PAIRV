#ifndef RV_NICE_PRIMITIVES_H
#define RV_NICE_PRIMITIVES_H

#include "nuclei_sdk_soc.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Low-level NICE primitives.
 *
 * These wrappers stay close to the current instruction contract and are meant
 * to be reused by PAIRV applications or higher-level kernel code. They are not
 * a SoC power-management API.
 *
 * Runtime policy in PAIRV:
 * - startup code clears MSTATUS.XS so NICE use-state starts disabled
 * - applications that issue NICE instructions must explicitly call
 *   RV_NICE_ENABLE() before use
 * - applications may call RV_NICE_DISABLE() when they are done
 */

__STATIC_FORCEINLINE bool rv_nice_hw_is_present(void)
{
    return (__RV_CSR_READ(CSR_MCFG_INFO) & MCFG_INFO_NICE) != 0u;
}

__STATIC_FORCEINLINE void rv_nice_enable(void)
{
    __RV_CSR_SET(CSR_MSTATUS, MSTATUS_XS);
}

__STATIC_FORCEINLINE void rv_nice_disable(void)
{
    __RV_CSR_CLEAR(CSR_MSTATUS, MSTATUS_XS);
}

__STATIC_FORCEINLINE bool rv_nice_is_enabled(void)
{
    return (__RV_CSR_READ(CSR_MSTATUS) & MSTATUS_XS) != 0u;
}

__STATIC_FORCEINLINE rv_csr_t rv_nice_read_cycles(void)
{
    return __RV_CSR_READ(CSR_MCYCLE);
}

#define RV_NICE_ENABLE() rv_nice_enable()
#define RV_NICE_DISABLE() rv_nice_disable()
#define RV_NICE_IS_ENABLED() rv_nice_is_enabled()
#define RV_NICE_HW_PRESENT() rv_nice_hw_is_present()

__STATIC_FORCEINLINE void rv_nice_bf16_setup(uint16_t *base_addr,
                                             uint16_t *target_addr)
{
    int zero = 0;

    asm volatile(".insn r 0xb, 3, 0, x0, %1, %2"
                 : "=r"(zero)
                 : "r"(base_addr), "r"(target_addr));
}

__STATIC_FORCEINLINE void rv_nice_bf16_wsetup(uint32_t word_len)
{
    int zero = 0;

    asm volatile(".insn r 0xb, 2, 0, x0, %1, x0" : "=r"(zero) : "r"(word_len));
}

__STATIC_FORCEINLINE uint16_t rv_nice_bf16_load_acc(uint16_t *base_addr)
{
    uint16_t acc_res;

    asm volatile(".insn r 0xb, 6, 1, %0, %1, x0"
                 : "=r"(acc_res)
                 : "r"(base_addr));
    return acc_res;
}

__STATIC_FORCEINLINE uint16_t rv_nice_bf16_div(uint16_t acc_res)
{
    uint16_t div_res;

    asm volatile(".insn r 0xb, 6, 2, %0, %1, x0"
                 : "=r"(div_res)
                 : "r"(acc_res));
    return div_res;
}

__STATIC_FORCEINLINE uint16_t rv_nice_bf16_square_sum(uint16_t *load_base_addr)
{
    uint16_t square_sum_res;

    asm volatile(".insn r 0xb, 6, 3, %0, %1, x0"
                 : "=r"(square_sum_res)
                 : "r"(load_base_addr));
    return square_sum_res;
}

__STATIC_FORCEINLINE void rv_nice_bf16_load_sub_store(uint16_t mean,
                                                      uint16_t *store_base_addr)
{
    int zero = 0;

    asm volatile(".insn r 0xb, 3, 4, x0, %1, %2"
                 : "=r"(zero)
                 : "r"(mean), "r"(store_base_addr));
}

__STATIC_FORCEINLINE uint16_t rv_nice_bf16_sqrt(uint16_t mean)
{
    uint16_t sqrt_mean = 0;

    asm volatile(".insn r 0xb, 6, 5, %0, %1, x0" : "=r"(sqrt_mean) : "r"(mean));
    return sqrt_mean;
}

__STATIC_FORCEINLINE void rv_nice_bf16_load_div_store(uint16_t *load_base_addr,
                                                      uint16_t sqrt_mean)
{
    int zero = 0;

    asm volatile(".insn r 0xb, 3, 6, x0, %1, %2"
                 : "=r"(zero)
                 : "r"(load_base_addr), "r"(sqrt_mean));
}

__STATIC_FORCEINLINE void rv_nice_bf16_load_exp_store(uint16_t *load_base_addr,
                                                      uint16_t *store_base_addr)
{
    int zero = 0;

    asm volatile(".insn r 0xb, 3, 7, x0, %1, %2"
                 : "=r"(zero)
                 : "r"(load_base_addr), "r"(store_base_addr));
}

#ifdef __cplusplus
}
#endif

#endif /* RV_NICE_PRIMITIVES_H */
