/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_ESP_AMP_SYSTEM_AUTO_LIGHT_SLEEP_SUPPORT_ENABLE && !IS_MAIN_CORE
/**
 * @brief Enter the critical section to skip the light sleep.
 */
void esp_amp_system_pm_subcore_skip_light_sleep_enter(void);

/**
 * @brief Exit the critical section to skip the light sleep.
 */
void esp_amp_system_pm_subcore_skip_light_sleep_exit(void);

#define ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER() do { esp_amp_system_pm_subcore_skip_light_sleep_enter(); } while (0)
#define ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT() do { esp_amp_system_pm_subcore_skip_light_sleep_exit(); } while (0)
#else /* !CONFIG_ESP_AMP_SYSTEM_AUTO_LIGHT_SLEEP_SUPPORT_ENABLE || IS_MAIN_CORE */
#define ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER() do { } while (0)
#define ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT() do { } while (0)
#endif /* CONFIG_ESP_AMP_SYSTEM_AUTO_LIGHT_SLEEP_SUPPORT_ENABLE && !IS_MAIN_CORE */

#ifdef __cplusplus
}
#endif
