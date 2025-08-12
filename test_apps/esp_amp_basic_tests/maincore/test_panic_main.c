/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_amp.h"
#include "esp_amp_panic.h"

#include "unity.h"
#include "unity_test_runner.h"

#define SYS_INFO_ID_TEST_BITS 0x0000
#define TEST_ID_ABORT 0x1
#define TEST_ID_ASSERT 0x2

#define SUBCORE_ABORT_STR  "abort() was called"
#define SUBCORE_ASSERT_STR "assert failed on subcore"

extern const uint8_t subcore_test_panic_bin_start[] asm("_binary_subcore_test_panic_bin_start");
extern const uint8_t subcore_test_panic_bin_end[]   asm("_binary_subcore_test_panic_bin_end");

/* global test variables */
bool g_test_is_subcore_abort = false;
bool g_test_is_subcore_assert = false;
bool g_test_is_subcore_panic = false;

static uint32_t g_test_id_value = 0;

static void subcore_test_panic_subcore_init(void)
{
    esp_amp_init();

    atomic_uint *test_bits = (atomic_uint *)esp_amp_sys_info_alloc(SYS_INFO_ID_TEST_BITS, sizeof(uint32_t), SYS_INFO_CAP_HP);
    atomic_init(test_bits, g_test_id_value);

    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_test_panic_bin_start));
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_start_subcore());

    printf("sub core loaded with firmware and running successfully\n");
}

void esp_amp_subcore_panic_handler(void)
{
    printf("received subcore panic\n");
    esp_amp_subcore_panic_dump_t *panic_dump = (esp_amp_subcore_panic_dump_t *)g_esp_amp_subcore_panic_dump;
    if (panic_dump->is_abort == 1) {
        if (strncmp(panic_dump->abort_details, SUBCORE_ABORT_STR, strlen(SUBCORE_ABORT_STR)) == 0) {
            g_test_is_subcore_abort = true;
        } else if (strncmp(panic_dump->abort_details, SUBCORE_ASSERT_STR, strlen(SUBCORE_ASSERT_STR)) == 0) {
            g_test_is_subcore_assert = true;
        }
    }

    extern void esp_amp_subcore_panic_handler_default(void);
    esp_amp_subcore_panic_handler_default();

    g_test_is_subcore_panic = true;
}

TEST_CASE("test abort on subcore", "[esp_amp]")
{
    g_test_id_value = TEST_ID_ABORT;
    subcore_test_panic_subcore_init();
    int timeout = 2000;
    while (g_test_is_subcore_panic == false && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout -= 100;
    }
    TEST_ASSERT_EQUAL(true, g_test_is_subcore_abort);
}

TEST_CASE("test assert on subcore", "[esp_amp]")
{
    g_test_id_value = TEST_ID_ASSERT;
    subcore_test_panic_subcore_init();
    int timeout = 2000;
    while (g_test_is_subcore_panic == false && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout -= 100;
    }
    TEST_ASSERT_EQUAL(true, g_test_is_subcore_assert);
}
