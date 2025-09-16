/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_amp.h"
#include "esp_err.h"
#include "esp_pm.h"

#include "unity.h"
#include "unity_test_runner.h"

static int enter_light_sleep_cnt = 0;
static int64_t last_sleep_duration = 0;

static esp_err_t enter_light_sleep_cb(int64_t sleep_time_us, void *arg)
{
    enter_light_sleep_cnt++;
    ESP_EARLY_LOGW("", "Entering light sleep mode, expected sleep time: %lld", sleep_time_us);
    return ESP_OK;
}

static esp_err_t exit_light_sleep_cb(int64_t sleep_time_us, void *arg)
{
    last_sleep_duration = sleep_time_us;
    ESP_EARLY_LOGW("", "Woken up from light sleep mode, actual sleep time: %lld", sleep_time_us);
    return ESP_OK;
}

#define TAG "app_main"

#define EVENT_SUBCORE_READY (BIT0 | BIT1 | BIT2 | BIT3)

extern const uint8_t subcore_event_test_bin_start[] asm("_binary_subcore_test_event_bin_start");
extern const uint8_t subcore_event_test_bin_end[] asm("_binary_subcore_test_event_bin_end");

TEST_CASE("maincore can receive subcore event during light sleep", "[esp_amp]")
{
    /* init esp amp component */
    assert(esp_amp_init() == 0);

    /* load and start subcore */
    ESP_ERROR_CHECK(esp_amp_load_sub(subcore_event_test_bin_start));
    ESP_ERROR_CHECK(esp_amp_start_subcore());

    uint32_t subcore_event_bits = esp_amp_event_wait(EVENT_SUBCORE_READY, true, true, 5000);
    TEST_ASSERT_EQUAL(EVENT_SUBCORE_READY, subcore_event_bits & EVENT_SUBCORE_READY);

    /* configure pm */
    esp_pm_sleep_cbs_register_config_t pm_callbacks = {.enter_cb = enter_light_sleep_cb,
                                                       .exit_cb = exit_light_sleep_cb,
                                                       .enter_cb_prior = 5,
                                                       .exit_cb_prior = 5,
                                                       .enter_cb_user_arg = 0,
                                                       .exit_cb_user_arg = 0
                                                      };
    ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&pm_callbacks));
    esp_pm_config_t pm_config = {.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_IDF_TARGET_ESP32C5
                                 .min_freq_mhz = 48,
#else
                                 .min_freq_mhz = CONFIG_XTAL_FREQ,
#endif
                                 .light_sleep_enable = true
                                };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    subcore_event_bits = esp_amp_event_wait(EVENT_SUBCORE_READY, true, true, 5000);
    TEST_ASSERT_EQUAL(EVENT_SUBCORE_READY, subcore_event_bits & EVENT_SUBCORE_READY);

    /* expected to wakeup at T=2000ms */
    TEST_ASSERT_EQUAL(1, enter_light_sleep_cnt);
    TEST_ASSERT_TRUE(last_sleep_duration > 1900000 && last_sleep_duration < 2100000);
}
