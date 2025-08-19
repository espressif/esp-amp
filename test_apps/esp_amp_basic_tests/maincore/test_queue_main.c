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

#include "esp_amp.h"

#include "unity.h"
#include "unity_test_runner.h"

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

TEST_CASE("test queue send and recv", "[esp_amp]")
{
    TEST_ASSERT(esp_amp_init() == 0);

    int queue_len = 8;
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

    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_queue_bin_start));
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_start_subcore());
    vTaskDelay(pdMS_TO_TICKS(1000)); /* wait for subcore to start */

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

    /*
     * wait until we handle all routed print requests from subcore
     * or we might confuse pytest-embedded
     */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
