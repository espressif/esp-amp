/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_pm.h"
#include "esp_log.h"

#include "esp_amp.h"
#include "esp_amp_panic.h"

#include "unity.h"
#include "unity_test_runner.h"

static int enter_light_sleep_cnt = 0;

static esp_err_t enter_light_sleep_cb(int64_t sleep_time_us, void *arg)
{
    enter_light_sleep_cnt++;
    ESP_EARLY_LOGW("", "Entering light sleep mode, expected sleep time: %lld", sleep_time_us);
    return ESP_OK;
}

static esp_err_t exit_light_sleep_cb(int64_t sleep_time_us, void *arg)
{
    ESP_EARLY_LOGW("", "Woken up from light sleep mode, actual sleep time: %lld", sleep_time_us);
    return ESP_OK;
}

#define SYS_INFO_ID_TEST_BITS 0x0000
#define TEST_ID_ABORT 0x1
#define TEST_ID_ASSERT 0x2

#define SUBCORE_ABORT_STR "abort() was called"
#define SUBCORE_ASSERT_STR "assert failed on subcore"

extern const uint8_t subcore_test_panic_bin_start[] asm("_binary_subcore_test_panic_bin_start");
extern const uint8_t subcore_test_panic_bin_end[] asm("_binary_subcore_test_panic_bin_end");

/* global test variables */
bool g_test_is_subcore_abort = false;
bool g_test_is_subcore_assert = false;
bool g_test_is_subcore_panic = false;

static uint32_t g_test_id_value = 0;

static void test_init(void)
{
    /* init esp amp component */
    esp_amp_init();

    /* init test variables */
    uint32_t *test_bits = (uint32_t *)esp_amp_sys_info_alloc(SYS_INFO_ID_TEST_BITS, sizeof(uint32_t), SYS_INFO_CAP_RTC);
    *test_bits = g_test_id_value;

    /* load and start subcore */
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_test_panic_bin_start));
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_start_subcore());

    /* wait for subcore ready */
    vTaskDelay(pdMS_TO_TICKS(500));

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
}

void esp_amp_subcore_panic_handler(void)
{
    printf("received subcore panic\n");
    esp_amp_subcore_panic_dump_t *panic_dump = (esp_amp_subcore_panic_dump_t *)g_esp_amp_subcore_panic_dump;
    if (panic_dump->is_abort == 1) {
        if (strncmp(panic_dump->abort_details, SUBCORE_ABORT_STR, strlen(SUBCORE_ABORT_STR)) == 0) {
            g_test_is_subcore_abort = true;
        } else if (strncmp(panic_dump->abort_details, SUBCORE_ASSERT_STR, strlen(SUBCORE_ASSERT_STR)) == 0) {
            g_test_is_subcore_abort = true;
        }
    }

    extern void esp_amp_subcore_panic_handler_default(void);
    esp_amp_subcore_panic_handler_default();

    g_test_is_subcore_panic = true;
}

TEST_CASE("abort error from subcore can be handled correctly during maincore light sleep", "[esp_amp]")
{
    g_test_id_value = TEST_ID_ABORT;
    test_init();
    vTaskDelay(pdMS_TO_TICKS(3000));
    TEST_ASSERT_EQUAL(true, g_test_is_subcore_abort);
    TEST_ASSERT_EQUAL(1 + 1, enter_light_sleep_cnt);
}

TEST_CASE("assert error from subcore can be handled correctly during maincore light sleep", "[esp_amp]")
{
    g_test_id_value = TEST_ID_ASSERT;
    test_init();
    vTaskDelay(pdMS_TO_TICKS(3000));
    TEST_ASSERT_EQUAL(true, g_test_is_subcore_abort);
    TEST_ASSERT_EQUAL(1 + 1, enter_light_sleep_cnt);
}
