/*
* SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include "stdint.h"
#include "esp_amp_arch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get cpu core id by reading register
 *
 * On RISC-V platform, this is done by reading the mhartid CSR
 *
 * @retval CORE_ID
 */
static inline int esp_amp_platform_get_core_id(void)
{
    return esp_amp_arch_get_core_id();
}

/**
 * Busy-looping delay for milli-second
 *
 * @param time delay duration (ms)
 */
void esp_amp_platform_delay_ms(uint32_t time);


/**
 * Busy-looping delay for micro-second
 *
 * @param time delay duration (us)
 */
void esp_amp_platform_delay_us(uint32_t time);


/**
 * @brief Get system timestamp in milliseconds
 *
 * @note on subcore, mcycle & mcycleh CSR is used to get timestamp
 * @note on maincore, esp_system_get_time() is used to get timestamp
 * @note mcycle stops counting when cpu is in sleep mode
 * @note timestamp on maincore and subcore can be different
 *
 * @retval current timestamp in milliseconds
 */
int64_t esp_amp_platform_get_time_ms(void);


/**
 * Disable all interrupts on local core
 *
 * @note to protect data in critical section, use esp_amp_env_enter_critical()
 * and esp_amp_env_exit_critical() instead.
 */
void esp_amp_platform_intr_disable(void);


/**
 * Enable interrupts on local core
 *
 * @note to protect data in critical section, use esp_amp_env_enter_critical()
 * and esp_amp_env_exit_critical() instead.
 */
void esp_amp_platform_intr_enable(void);


/**
 * Enable software interrupt on local core
 */
void esp_amp_platform_sw_intr_enable(void);


/**
 * Disable software interrupt on local core
 */
void esp_amp_platform_sw_intr_disable(void);


/**
 * Install software interrupt
 *
 * @retval 0 if successful, -1 if failed
 */
int esp_amp_platform_sw_intr_install(void);


/**
 * Trigger software interrupt on remote core
 */
void esp_amp_platform_sw_intr_trigger(void);


/**
 * Clear software interrupt triggered by remote core
 */
void esp_amp_platform_sw_intr_clear(void);


/**
 * Memory barrier
 */
static inline void esp_amp_platform_memory_barrier(void)
{
    esp_amp_arch_memory_barrier();
}

#ifdef __cplusplus
}
#endif
