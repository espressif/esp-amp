/*
* SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#include "limits.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_interrupts.h"

#include "esp_amp_arch.h"
#include "esp_amp_platform.h"

#define LP_CORE_CPU_FREQ_HZ 16000000

void esp_amp_platform_delay_us(uint32_t time)
{
    ulp_lp_core_delay_us(time);
}

void esp_amp_platform_delay_ms(uint32_t time)
{
    if (time >= INT_MAX / 1000) { /* if overflow */
        ulp_lp_core_delay_us(INT_MAX);
    } else {
        ulp_lp_core_delay_us(time * 1000);
    }
}

int64_t esp_amp_platform_get_time_ms(void)
{
    return esp_amp_arch_get_cpu_cycle_64() / (LP_CORE_CPU_FREQ_HZ / 1000);
}

void esp_amp_platform_intr_enable(void)
{
    ulp_lp_core_intr_enable();
}

void esp_amp_platform_intr_disable(void)
{
    ulp_lp_core_intr_disable();
}
