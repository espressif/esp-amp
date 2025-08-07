/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "esp_amp_sys_info.h"
#include "esp_rom_uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_pm.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_amp.h"

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

extern const uint8_t subcore_sw_intr_bin_start[] asm("_binary_subcore_test_sw_intr_bin_start");
extern const uint8_t subcore_sw_intr_bin_end[] asm("_binary_subcore_test_sw_intr_bin_end");

static uint8_t main_sw_intr_record[4];

static IRAM_ATTR int sw_intr_id0_handler(void *arg)
{
    esp_rom_printf("sw_intr_id0_handler() called\n");
    esp_rom_output_flush_tx(0);
    main_sw_intr_record[0]++;
    return 0;
}

static IRAM_ATTR int sw_intr_id1_handler(void *arg)
{
    esp_rom_printf("sw_intr_id1_handler() called\n");
    esp_rom_output_flush_tx(0);
    main_sw_intr_record[1]++;
    return 0;
}

static IRAM_ATTR int sw_intr_id2_handler(void *arg)
{
    esp_rom_printf("sw_intr_id2_handler() called\n");
    esp_rom_output_flush_tx(0);
    main_sw_intr_record[2]++;
    return 0;
}

static IRAM_ATTR int sw_intr_id3_handler(void *arg)
{
    esp_rom_printf("sw_intr_id3_handler() called\n");
    esp_rom_output_flush_tx(0);
    main_sw_intr_record[3]++;
    return 0;
}

TEST_CASE("sw interrupt from subcore can be handled correctly during maincore light sleep", "[esp_amp]")
{
    /* init esp amp component */
    TEST_ASSERT(esp_amp_init() == 0);

    /* register sw interrupt handler */
    TEST_ASSERT(esp_amp_sw_intr_add_handler(SW_INTR_ID_0, sw_intr_id0_handler, NULL) == 0);
    TEST_ASSERT(esp_amp_sw_intr_add_handler(SW_INTR_ID_1, sw_intr_id1_handler, NULL) == 0);
    TEST_ASSERT(esp_amp_sw_intr_add_handler(SW_INTR_ID_2, sw_intr_id2_handler, NULL) == 0);
    TEST_ASSERT(esp_amp_sw_intr_add_handler(SW_INTR_ID_3, sw_intr_id3_handler, NULL) == 0);
    esp_amp_sw_intr_handler_dump();

    /* load and start subcore */
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_sw_intr_bin_start));
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

    /* wait for subcore to send sw interrupt */
    vTaskDelay(pdMS_TO_TICKS(8000));

    /* check sleep count */
    TEST_ASSERT_EQUAL(4 * 16 + 1, enter_light_sleep_cnt);

    /* check sw interrupt record */
    uint8_t main_sw_intr_expect[4] = {0x10, 0x10, 0x10, 0x10};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(main_sw_intr_expect, main_sw_intr_record, 4);

    /*
     * wait until we handle all routed print requests from subcore
     * or we might confuse pytest-embedded
     */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
