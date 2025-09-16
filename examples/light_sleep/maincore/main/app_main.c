/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <sdkconfig.h>
#include <stdio.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_amp.h"
#include "esp_pm.h"

#include "event.h"

static esp_err_t enter_light_sleep_cb(int64_t sleep_time_us, void *arg)
{
    ESP_EARLY_LOGW("", "Entering light sleep mode, expected sleep time: %lld", sleep_time_us);
    return ESP_OK;
}

static esp_err_t exit_light_sleep_cb(int64_t sleep_time_us, void *arg)
{
    ESP_EARLY_LOGW("", "Woken up from light sleep mode, actual sleep time: %lld", sleep_time_us);
    return ESP_OK;
}

static const DRAM_ATTR char TAG[] = "app_main";

static esp_amp_rpmsg_dev_t rpmsg_dev;
static esp_amp_rpmsg_ept_t rpmsg_ept;

static void ept_task_ctx(void *arg)
{
    QueueHandle_t msgQueue = (QueueHandle_t)arg;
    void *data;

    while (true) {
        xQueueReceive(msgQueue, &data, portMAX_DELAY);

        int sum = *((int *)data);
        ESP_LOGI(TAG, "maincore: received sum=%d, echoing back", sum);
        esp_amp_rpmsg_destroy(&rpmsg_dev, data);

        int *out = (int *)esp_amp_rpmsg_create_message(&rpmsg_dev, sizeof(int), ESP_AMP_RPMSG_DATA_DEFAULT);
        if (out) {
            *out = sum;
            assert(esp_amp_rpmsg_send_nocopy(&rpmsg_dev, &rpmsg_ept, 0, out, sizeof(int)) == 0);
        } else {
            ESP_LOGE(TAG, "maincore: failed to allocate echo message");
        }
    }
    vTaskDelete(NULL);
}

static int IRAM_ATTR ept_cb(void *msg_data, uint16_t data_len, uint16_t src_addr, void *rx_cb_data)
{
    QueueHandle_t msgQueue = (QueueHandle_t)rx_cb_data;

    if (esp_amp_env_in_isr()) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (xQueueSendFromISR(msgQueue, &msg_data, &xHigherPriorityTaskWoken) != pdTRUE) {
            esp_amp_rpmsg_destroy(&rpmsg_dev, msg_data);
        }
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
        if (xQueueSend(msgQueue, &msg_data, pdMS_TO_TICKS(10)) != pdTRUE) {
            esp_amp_rpmsg_destroy(&rpmsg_dev, msg_data);
        }
    }
    return 0;
}

void app_main(void)
{
    /* init esp amp component */
    assert(esp_amp_init() == 0);

    /* init rpmsg related resource */
    int ret;
    ret = esp_amp_rpmsg_main_init(&rpmsg_dev, 4, 32, true, false);
    assert(ret == 0);

    QueueHandle_t ept_msg_queue = xQueueCreate(8, sizeof(void *));
    assert(ept_msg_queue != NULL);

    assert(esp_amp_rpmsg_create_endpoint(&rpmsg_dev, 0, ept_cb, (void *)ept_msg_queue, &rpmsg_ept) != NULL);
    esp_amp_rpmsg_intr_enable(&rpmsg_dev);
    xTaskCreate(ept_task_ctx, "ept0", 2048, (void *)ept_msg_queue, tskIDLE_PRIORITY + 1, NULL);

    /* load and start subcore firmware */
    const esp_partition_t *sub_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, NULL);
    ESP_ERROR_CHECK(esp_amp_load_sub_from_partition(sub_partition));
    ESP_ERROR_CHECK(esp_amp_start_subcore());

    /* wait for link up */
    assert((esp_amp_event_wait(EVENT_SUBCORE_READY, true, true, 10000) & EVENT_SUBCORE_READY) == EVENT_SUBCORE_READY);

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

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(portMAX_DELAY));
    }
}
