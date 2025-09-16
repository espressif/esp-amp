/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "esp_amp_sys_info.h"
#include "esp_amp_sw_intr.h"
#include "esp_amp_platform.h"
#include "esp_amp_event.h"
#include "esp_amp_log.h"
#include "esp_amp_pm.h"

#ifdef __cplusplus
#include <atomic>
using std::atomic_int;
#else
#include <stdatomic.h>
#endif

#define TAG "event"

typedef struct {
    uint16_t sysinfo_id;
    void *event_group;
    atomic_int *event_bits;
} esp_amp_event_t;

uint32_t esp_amp_event_notify_by_id(uint16_t sysinfo_id, uint32_t bit_mask)
{
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();

    uint16_t event_bits_size = 0;
    atomic_int *event_bits = (atomic_int *)esp_amp_sys_info_get(sysinfo_id, &event_bits_size, SYS_INFO_CAP_HP);
    assert(event_bits != NULL && event_bits_size == sizeof(atomic_int));

    uint32_t ret_val = atomic_fetch_or_explicit(event_bits, bit_mask, memory_order_seq_cst);
    ESP_AMP_LOGD(TAG, "notify event(%p) %p", event_bits, (void *)bit_mask);

    esp_amp_sw_intr_trigger(SW_INTR_RESERVED_ID_EVENT);

    ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();

    return ret_val;
}

uint32_t esp_amp_event_wait_by_id(uint16_t sysinfo_id, uint32_t bit_mask, bool clear_on_exit, bool wait_for_all,
                                  uint32_t timeout_ms)
{
    int ret = 0;
    uint32_t cur_time = esp_amp_platform_get_time_ms();
    uint32_t expected = bit_mask;
    uint32_t desired = 0;
    if (clear_on_exit == false) {
        desired = expected; /* clear all expected event bit */
    }

    ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();

    uint16_t event_bits_size = 0;
    atomic_uint *event_bits = esp_amp_sys_info_get(sysinfo_id, &event_bits_size, SYS_INFO_CAP_HP);
    assert(event_bits != NULL && event_bits_size == sizeof(atomic_uint));

    if (wait_for_all) { /* wait for all */
        while (!atomic_compare_exchange_weak(event_bits, &expected, desired)) {
            uint32_t actual = expected;
            /* after cmp&exchg, expect := actual */
            expected |= bit_mask; /* we expect event bit set */

            if (clear_on_exit == true) {
                desired = expected & ~bit_mask; /* clear all expected event bit */
            } else {
                desired = expected; /* keep all expected event bit unchanged */
            }

            /* if not all expected event bit set, don't wait */
            if ((actual & bit_mask) != bit_mask) {
                /* if timeout */
                if (esp_amp_platform_get_time_ms() - cur_time > timeout_ms) {
                    /* expected := actual, otherwise retval will always contain bit_mask set */
                    expected = actual;
                    break;
                }
            }
            /* if all expected event bit set, rerun cmp&exchg to write back */
            /* this may fail several times, due to modification from other core */
            /* up to 32 times (32-bit to set), it will succeed */
        }
        ret = expected;
    } else { /* wait for any */
        do {
            if (clear_on_exit == true) {
                ret = atomic_fetch_and(event_bits, ~bit_mask); /* clear any expected event bit */
            } else {
                ret = atomic_load(event_bits);
            }

            /* if timeout */
            if (esp_amp_platform_get_time_ms() - cur_time > timeout_ms) {
                break;
            }
        } while ((ret & bit_mask) == 0);
    }

    ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();

    return ret;
}

uint32_t esp_amp_event_clear_by_id(uint16_t sysinfo_id, uint32_t bit_mask)
{
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();

    uint16_t event_bits_size = 0;
    atomic_int *event_bits = esp_amp_sys_info_get(sysinfo_id, &event_bits_size, SYS_INFO_CAP_HP);
    assert(event_bits != NULL && event_bits_size == sizeof(atomic_int));

    int expected = 0;
    int desired = 0;
    while (!atomic_compare_exchange_weak(event_bits, &expected, desired)) {
        desired = expected & (~bit_mask);
    }

    ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();

    return expected;
}

int esp_amp_event_init(void)
{
    /* get event bit */
    atomic_int *main_core_event_bits =
        (atomic_int *)esp_amp_sys_info_get(SYS_INFO_RESERVED_ID_EVENT_MAIN, NULL, SYS_INFO_CAP_HP);
    atomic_int *sub_core_event_bits =
        (atomic_int *)esp_amp_sys_info_get(SYS_INFO_RESERVED_ID_EVENT_SUB, NULL, SYS_INFO_CAP_HP);

    if (main_core_event_bits == NULL || sub_core_event_bits == NULL) {
        ESP_AMP_LOGE(TAG, "Failed to init default event");
        return -1;
    }

    return 0;
}
