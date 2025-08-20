/*
* SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include "stdint.h"
#include "riscv/rv_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline int esp_amp_arch_get_core_id(void)
{
#ifdef __riscv
    return RV_READ_CSR(mhartid);
#endif
}

static inline void esp_amp_arch_memory_barrier(void)
{
#ifdef __riscv
    asm volatile("fence" ::: "memory");
#endif
}

static inline void esp_amp_arch_intr_enable(void)
{
#ifdef __riscv
    RV_SET_CSR(mstatus, MSTATUS_MIE);
#endif
}

static inline void esp_amp_arch_intr_disable(void)
{
#ifdef __riscv
    RV_CLEAR_CSR(mstatus, MSTATUS_MIE);
#endif
}

static inline uint32_t esp_amp_arch_get_cpu_cycle(void)
{
#ifdef __riscv
#if IS_MAIN_CORE
    return rv_utils_get_cycle_count();
#else
    return RV_READ_CSR(mcycle);
#endif
#else
#error "Unsupported architecture"
#endif
}

/**
 * @brief Get the CPU cycle in double word
 *
 * @note This function is only available on subcore
 */
#if !IS_MAIN_CORE
typedef union {
    struct {
        uint32_t rv_mcycle;
        uint32_t rv_mcycleh;
    };
    uint64_t rv_mcycle_comb;
} esp_cpu_rv_mcycle_t;

static inline uint64_t esp_amp_arch_get_cpu_cycle_64(void)
{
    esp_cpu_rv_mcycle_t cpu_cycle;
    uint32_t mcycleh_snapshot;

    /* double-check mcycleh: to avoid inconsistency when (mcycleh, mcycle) */
    /* changes from (0x0, 0xffff_ffff) to (0x1, 0x0000_0000) */
    mcycleh_snapshot = RV_READ_CSR(mcycleh);

    do {
        cpu_cycle.rv_mcycle = RV_READ_CSR(mcycle);
        cpu_cycle.rv_mcycleh = RV_READ_CSR(mcycleh);
    } while (mcycleh_snapshot != cpu_cycle.rv_mcycleh);

    return cpu_cycle.rv_mcycle_comb;
}
#endif /* !IS_MAIN_CORE */

#ifdef __cplusplus
}
#endif
