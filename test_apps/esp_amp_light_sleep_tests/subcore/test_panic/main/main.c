/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <assert.h>

#include "esp_amp.h"

#define SYS_INFO_ID_TEST_BITS 0x0000
#define TEST_ID_ABORT 0x1
#define TEST_ID_ASSERT 0x2

void test_abort(void)
{
    abort();
}

void test_assert(void)
{
    assert(0);
}

int main(void)
{
    esp_amp_init();

    uint32_t *test_bits = (uint32_t *)esp_amp_sys_info_get(SYS_INFO_ID_TEST_BITS, NULL, SYS_INFO_CAP_RTC);

    esp_amp_platform_delay_ms(1000);

    if (*test_bits == TEST_ID_ABORT) {
        test_abort();
    } else if (*test_bits == TEST_ID_ASSERT) {
        test_assert();
    }
    for (;;)
        ;
}
