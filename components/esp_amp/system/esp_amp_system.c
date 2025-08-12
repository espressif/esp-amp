/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"

#include "esp_amp_service.h"
#include "esp_amp_system.h"
#include "esp_amp_system_priv.h"
#include "esp_amp_pm_priv.h"

int esp_amp_system_init(void)
{
    int ret = 0;

#if CONFIG_ESP_AMP_SYSTEM_AUTO_LIGHT_SLEEP_SUPPORT_ENABLE
    ret = esp_amp_system_pm_init();
    if (ret != 0) {
        goto exit;
    }
#endif

#if CONFIG_ESP_AMP_SYSTEM_ENABLE_SUPPLICANT
    ret = esp_amp_system_service_init();
    if (ret != 0) {
        goto exit;
    }
#endif

#if IS_MAIN_CORE
    ret = esp_amp_system_panic_init();
    if (ret != 0) {
        goto exit;
    }
#endif

exit:
    return ret;
}
