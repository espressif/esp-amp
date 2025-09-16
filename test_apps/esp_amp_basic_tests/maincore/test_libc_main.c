/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "esp_amp.h"
#include "esp_err.h"
#include "stdatomic.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "unity.h"
#include "unity_test_runner.h"

extern const uint8_t subcore_libc_sub_test_bin_start[] asm("_binary_subcore_test_libc_bin_start");
extern const uint8_t subcore_libc_sub_test_bin_end[]   asm("_binary_subcore_test_libc_bin_end");

#define SYS_INFO_ID_TEST_BITS 0x0000
#define SW_INTR_ID_ISR_FLOAT  SW_INTR_ID_0
#define EVENT_SUBCORE_READY   (1 << 0)

#define TEST_MATH (1 << 0)
#define TEST_CPP_INIT_ARRAY (1 << 1)
#define TEST_MEMORY (1 << 2)
#define TEST_STRING (1 << 3)
#define TEST_FLOAT_IN_ISR (1 << 4)
#define TEST_ALL (TEST_MATH | TEST_CPP_INIT_ARRAY | TEST_MEMORY | TEST_STRING | TEST_FLOAT_IN_ISR)

TEST_CASE("libc functions on subcore", "[esp_amp]")
{
    esp_amp_init();
    atomic_uint *test_bits = (atomic_uint *)esp_amp_sys_info_alloc(SYS_INFO_ID_TEST_BITS, sizeof(uint32_t), SYS_INFO_CAP_HP);
    atomic_init(test_bits, 0);

    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_libc_sub_test_bin_start));
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_start_subcore());

    TEST_ASSERT_EQUAL(EVENT_SUBCORE_READY & esp_amp_event_wait(EVENT_SUBCORE_READY, true, true, 5000), EVENT_SUBCORE_READY);

    esp_amp_sw_intr_trigger(SW_INTR_ID_ISR_FLOAT);

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    uint32_t test_bits_val = atomic_load(test_bits);
    printf("test_bits = %lu\n", test_bits_val);
    TEST_ASSERT_EQUAL(test_bits_val, TEST_ALL);
}
