/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_amp_sys_info.h"

#if IS_MAIN_CORE
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_sleep.h"
#else /* !IS_MAIN_CORE */
#include "esp_amp_env.h"
#include "esp_amp_platform.h"
#endif /* IS_MAIN_CORE */

static uint8_t *lp_access_hp_cnt = NULL;

#if IS_MAIN_CORE
typedef bool (*skip_light_sleep_cb_t)(void);
extern esp_err_t esp_pm_register_skip_light_sleep_callback(skip_light_sleep_cb_t cb);

static IRAM_ATTR bool esp_amp_system_pm_maincore_check_ls_ctrl(void)
{
    return *lp_access_hp_cnt > 0;
}

#else /* !IS_MAIN_CORE */
void esp_amp_system_pm_subcore_skip_light_sleep_enter(void)
{
    bool need_trigger = false;

    esp_amp_env_enter_critical();
    need_trigger = *lp_access_hp_cnt == 0;
    esp_amp_platform_memory_barrier();
    *lp_access_hp_cnt = *lp_access_hp_cnt + 1;
    esp_amp_env_exit_critical();

    if (!need_trigger) {
        return;
    }
    esp_amp_platform_memory_barrier();
    esp_amp_platform_sw_intr_trigger();
}

void esp_amp_system_pm_subcore_skip_light_sleep_exit(void)
{
    esp_amp_env_enter_critical();
    assert(*lp_access_hp_cnt > 0);
    esp_amp_platform_memory_barrier();
    *lp_access_hp_cnt = *lp_access_hp_cnt - 1;
    esp_amp_env_exit_critical();
}

#endif /* IS_MAIN_CORE */

int esp_amp_system_pm_init(void)
{
    int ret = -1;

#if IS_MAIN_CORE
    lp_access_hp_cnt = esp_amp_sys_info_alloc(SYS_INFO_RESERVED_ID_PM, sizeof(lp_access_hp_cnt), SYS_INFO_CAP_RTC);
    if (lp_access_hp_cnt == NULL) {
        goto exit;
    }
    esp_sleep_enable_ulp_wakeup();
    *lp_access_hp_cnt = 0;
    if (esp_pm_register_skip_light_sleep_callback(esp_amp_system_pm_maincore_check_ls_ctrl) != ESP_OK) {
        goto exit;
    };
#else  /* !IS_MAIN_CORE */
    lp_access_hp_cnt = esp_amp_sys_info_get(SYS_INFO_RESERVED_ID_PM, NULL, SYS_INFO_CAP_RTC);
    if (lp_access_hp_cnt == NULL) {
        goto exit;
    }
#endif /* IS_MAIN_CORE */
    ret = 0;

exit:
    return ret;
}
