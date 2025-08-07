/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_pm.h"
#include "esp_log.h"
#include "esp_amp.h"
#include "esp_timer.h"

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

extern const uint8_t subcore_queue_bin_start[] asm("_binary_subcore_test_queue_bin_start");
extern const uint8_t subcore_queue_bin_end[] asm("_binary_subcore_test_queue_bin_end");

static IRAM_ATTR int vq_send_isr(void *args)
{
    esp_amp_sw_intr_trigger(SW_INTR_ID_0);
    return 0;
}

static IRAM_ATTR int vq_recv_isr(void *args)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    SemaphoreHandle_t semaphore = (SemaphoreHandle_t)args;
    xSemaphoreGiveFromISR(semaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    return 0;
}

static void recv_ints_blocking(esp_amp_queue_t *queue, SemaphoreHandle_t sem, int expected, int *out_buffer)
{
    int received = 0;
    int *buf = NULL;
    uint16_t buf_size = 0;

    while (received < expected) {
        while (received < expected && (esp_amp_queue_recv_try(queue, (void **)(&buf), &buf_size) == 0)) {
            TEST_ASSERT_EQUAL(buf_size, sizeof(int));
            printf("maincore received %d from subcore\n", *buf);
            out_buffer[received++] = *buf;
            ESP_ERROR_CHECK(esp_amp_queue_free_try(queue, (void *)(buf)));
        }
        if (received < expected) {
            xSemaphoreTake(sem, portMAX_DELAY);
        }
    }
}

TEST_CASE("queue send and recieve can work correctly with light sleep", "[esp_amp]")
{
    /* init esp amp component */
    TEST_ASSERT(esp_amp_init() == 0);

    /* init queues */
    int queue_len = 4;
    int queue_item_size = 4;

    esp_amp_queue_t *vq_mc_master = (esp_amp_queue_t *)(malloc(sizeof(esp_amp_queue_t)));
    assert(esp_amp_queue_main_init(vq_mc_master, queue_len, queue_item_size, vq_send_isr, NULL, true, 0) == 0);

    SemaphoreHandle_t semaphore = xSemaphoreCreateCounting(16, 0);
    esp_amp_queue_t *vq_sc_master = (esp_amp_queue_t *)(malloc(sizeof(esp_amp_queue_t)));
    assert(esp_amp_queue_main_init(vq_sc_master, queue_len, queue_item_size, vq_recv_isr, (void *)(semaphore), false,
                                   1) == 0);
    esp_amp_queue_intr_enable(vq_sc_master, SW_INTR_ID_1);

    esp_amp_sys_info_dump();
    esp_amp_sw_intr_handler_dump();

    /* seed random number generator */
    srand(esp_timer_get_time());

    /* load and start subcore */
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_queue_bin_start));
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

    /* main test loop */
    int *buf = NULL;

    for (int i = 0; i < 8; i++) {
        /* subcore sends 2 numbers to maincore, maincore sends their sum back to subcore */
        int a_and_b[2] = {0};
        recv_ints_blocking(vq_sc_master, semaphore, 2, a_and_b);

        ESP_ERROR_CHECK(esp_amp_queue_alloc_try(vq_mc_master, (void **)(&buf), sizeof(int)));
        *buf = a_and_b[0] + a_and_b[1];
        ESP_ERROR_CHECK(esp_amp_queue_send_try(vq_mc_master, (void *)(buf), sizeof(int)));
        printf("maincore sent %d to subcore\n", a_and_b[0] + a_and_b[1]);

        /* maincore sends 2 numbers to subcore, subcore sends their sum back to maincore */
        int c = rand() % 1000;
        ESP_ERROR_CHECK(esp_amp_queue_alloc_try(vq_mc_master, (void **)(&buf), sizeof(int)));
        *buf = c;
        ESP_ERROR_CHECK(esp_amp_queue_send_try(vq_mc_master, (void *)(buf), sizeof(int)));
        printf("maincore sent %d to subcore\n", c);

        int d = rand() % 1000;
        ESP_ERROR_CHECK(esp_amp_queue_alloc_try(vq_mc_master, (void **)(&buf), sizeof(int)));
        *buf = d;
        ESP_ERROR_CHECK(esp_amp_queue_send_try(vq_mc_master, (void *)(buf), sizeof(int)));
        printf("maincore sent %d to subcore\n", d);

        int sum_from_sub = 0;
        recv_ints_blocking(vq_sc_master, semaphore, 1, &sum_from_sub);
        TEST_ASSERT_EQUAL(sum_from_sub, c + d);
    }

    TEST_ASSERT_EQUAL(enter_light_sleep_cnt, 3 * 8);

    /*
     * wait until we handle all routed print requests from subcore
     * or we might confuse pytest-embedded
     */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
