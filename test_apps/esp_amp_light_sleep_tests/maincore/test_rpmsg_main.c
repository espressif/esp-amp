/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_amp.h"
#include "esp_pm.h"
#include "esp_log.h"

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

extern const uint8_t subcore_rpmsg_bin_start[] asm("_binary_subcore_test_rpmsg_bin_start");
extern const uint8_t subcore_rpmsg_bin_end[] asm("_binary_subcore_test_rpmsg_bin_end");

#define MAGIC_TAG 0x12345678
#define FROM_MAIN 1
#define FROM_SUB 2

typedef struct {
    int magic;
    int from;
    int idx;
} simple_msg_t;

typedef struct {
    esp_amp_rpmsg_dev_t *rpmsg_dev;
    QueueHandle_t q_rx;
    SemaphoreHandle_t done;
} rx_ctx_t;

static int IRAM_ATTR ept_rx_isr(void *msg_data, uint16_t data_len, uint16_t src_addr, void *rx_cb_data)
{
    (void)data_len;
    (void)src_addr;

    rx_ctx_t *ctx = (rx_ctx_t *)rx_cb_data;
    BaseType_t hpw = pdFALSE;
    if (xQueueSendFromISR(ctx->q_rx, &msg_data, &hpw) != pdTRUE) {
        esp_amp_rpmsg_destroy(ctx->rpmsg_dev, msg_data);
    }
    portYIELD_FROM_ISR(hpw);
    return 0;
}

static void rx_task(void *arg)
{
    rx_ctx_t *ctx = (rx_ctx_t *)arg;
    int expected_idx = 0;

    for (int i = 0; i < 4; i++) {
        void *ptr = NULL;
        TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(ctx->q_rx, &ptr, portMAX_DELAY));
        simple_msg_t *m = (simple_msg_t *)ptr;

        /* simple validation */
        TEST_ASSERT_EQUAL(sizeof(simple_msg_t), sizeof(*m));
        TEST_ASSERT_EQUAL(MAGIC_TAG, m->magic);
        TEST_ASSERT_EQUAL(FROM_SUB, m->from);
        TEST_ASSERT_EQUAL(expected_idx, m->idx);
        expected_idx++;

        printf("%d: maincore recieved message<magic=%x from=%d idx=%d> from subcore\n", i, m->magic, m->from, m->idx);
        TEST_ASSERT_EQUAL(0, esp_amp_rpmsg_destroy(ctx->rpmsg_dev, m));
    }

    xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

TEST_CASE("rpmsg poll and recieve from subcore behaves correctly during maincore light sleep", "[esp_amp]")
{
    /* init esp amp component */
    TEST_ASSERT_EQUAL_INT(0, esp_amp_init());

    /* init RPMsg transport */
    esp_amp_rpmsg_dev_t *rpmsg_dev = (esp_amp_rpmsg_dev_t *)malloc(sizeof(esp_amp_rpmsg_dev_t));
    TEST_ASSERT_NOT_NULL(rpmsg_dev);
    TEST_ASSERT_EQUAL(0, esp_amp_rpmsg_main_init(rpmsg_dev, 16, 64, false, false));

    /* endpoint 0 + RX queue/semaphore */
    QueueHandle_t q_rx = xQueueCreate(8, sizeof(void *));
    TEST_ASSERT_NOT_NULL(q_rx);
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(done);

    esp_amp_rpmsg_ept_t *ept0 = (esp_amp_rpmsg_ept_t *)malloc(sizeof(esp_amp_rpmsg_ept_t));
    TEST_ASSERT_NOT_NULL(ept0);

    rx_ctx_t rx_ctx = {.rpmsg_dev = rpmsg_dev, .q_rx = q_rx, .done = done};
    TEST_ASSERT_EQUAL_HEX32(ept0, esp_amp_rpmsg_create_endpoint(rpmsg_dev, 0, ept_rx_isr, (void *)&rx_ctx, ept0));
    TEST_ASSERT_EQUAL_INT(0, esp_amp_rpmsg_intr_enable(rpmsg_dev));

    /* start RX task */
    TEST_ASSERT_NOT_EQUAL(pdFAIL, xTaskCreate(rx_task, "rpmsg_rx", 2048, (void *)&rx_ctx, tskIDLE_PRIORITY + 1, NULL));

    /* load and start subcore firmware */
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_rpmsg_bin_start));
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_start_subcore());

    /* wait for subcore ready */
    vTaskDelay(pdMS_TO_TICKS(200));

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

    /* wait for RX task to validate 4 inbound messages */
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(done, pdMS_TO_TICKS(5000)));
    TEST_ASSERT_EQUAL(1 * 4, enter_light_sleep_cnt);

    /* cleanup */
    vQueueDelete(q_rx);
    vSemaphoreDelete(done);
    free(ept0);
    free(rpmsg_dev);

    /*
     * wait until we handle all routed print requests from subcore
     * or we might confuse pytest-embedded
     */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
