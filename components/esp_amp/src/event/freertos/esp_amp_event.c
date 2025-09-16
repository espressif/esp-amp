/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#include "sdkconfig.h"
#include "string.h"
#include "esp_attr.h"

#ifdef __cplusplus
#include <atomic>
using std::atomic_int;
#else
#include <stdatomic.h>
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_amp_sw_intr.h"
#include "esp_amp_platform.h"
#include "esp_amp_log.h"
#include "esp_amp_sys_info.h"
#include "esp_amp_event.h"

static const DRAM_ATTR char TAG[] = "event";

/* default event storage */
static StaticEventGroup_t default_event_storage;

typedef struct {
    uint16_t sysinfo_id;
    uint16_t direction; /* not used for now */
    void *event_handle;
    atomic_uint *event_bits;
} esp_amp_event_t;

/**
 * Event Table
 * when SW_INTR_RESERVED_ID_EVENT is triggered, ISR will loop against event table
 * to find the corresponding event handle and event bits to set
 */
#define ESP_AMP_EVENT_TABLE_LEN (CONFIG_ESP_AMP_EVENT_TABLE_LEN + 1)
static esp_amp_event_t event_table[ESP_AMP_EVENT_TABLE_LEN]; /* one more entry for default group */
static portMUX_TYPE event_lock = portMUX_INITIALIZER_UNLOCKED;

/**
 * ISR for freertos event group
 *
 * loop against event table and set bits for each entry
 */
static IRAM_ATTR int os_env_event_isr(void *args)
{
    (void)args;
    BaseType_t need_yield = 0;

    portENTER_CRITICAL_ISR(&event_lock);
    for (int i = 0; i < ESP_AMP_EVENT_TABLE_LEN; i++) {
        if (event_table[i].event_handle == NULL) {
            continue;
        }

        uint32_t unprocessed = 0;
        BaseType_t task_yield = 0;

        while (!atomic_compare_exchange_weak(event_table[i].event_bits, &unprocessed, 0))
            ;
        ESP_AMP_DRAM_LOGD(TAG, "got event: sysinfo=%04x, unprocessed=%p", event_table[i].sysinfo_id,
                          (void *)unprocessed);
        xEventGroupSetBitsFromISR(event_table[i].event_handle, unprocessed, &task_yield);
        need_yield |= task_yield;
    }
    portEXIT_CRITICAL_ISR(&event_lock);

    return need_yield;
}

uint32_t IRAM_ATTR esp_amp_event_notify_by_id(uint16_t sysinfo_id, uint32_t bit_mask)
{
    uint16_t event_bits_size = 0;
    atomic_int *event_bits = (atomic_int *)esp_amp_sys_info_get(sysinfo_id, &event_bits_size, SYS_INFO_CAP_HP);
    assert(event_bits != NULL && event_bits_size == sizeof(atomic_int));

    uint32_t ret_val = atomic_fetch_or_explicit(event_bits, bit_mask, memory_order_seq_cst);

    ESP_AMP_DRAM_LOGD(TAG, "notify event(%p): %p", event_bits, (void *)bit_mask);
    esp_amp_sw_intr_trigger(SW_INTR_RESERVED_ID_EVENT);
    return ret_val;
}

uint32_t esp_amp_event_wait_by_id(uint16_t sysinfo_id, uint32_t bit_mask, bool clear_on_exit, bool wait_for_all,
                                  uint32_t timeout)
{
    EventGroupHandle_t event_handle = NULL;
    portENTER_CRITICAL(&event_lock);
    for (int i = 0; i < ESP_AMP_EVENT_TABLE_LEN; i++) {
        if (event_table[i].sysinfo_id == sysinfo_id) {
            event_handle = event_table[i].event_handle;
            break;
        }
    }
    portEXIT_CRITICAL(&event_lock);

    assert(event_handle != NULL);

    uint32_t timeout_tick = portMAX_DELAY;
    if (timeout != UINT_MAX) {
        timeout_tick = pdMS_TO_TICKS(timeout);
    }
    EventBits_t bits = xEventGroupWaitBits(event_handle, bit_mask, clear_on_exit, wait_for_all, timeout_tick);
    return bits;
}

uint32_t esp_amp_event_clear_by_id(uint16_t sysinfo_id, uint32_t bit_mask)
{
    EventGroupHandle_t event_handle = NULL;
    portENTER_CRITICAL(&event_lock);
    for (int i = 0; i < ESP_AMP_EVENT_TABLE_LEN; i++) {
        if (event_table[i].sysinfo_id == sysinfo_id) {
            event_handle = event_table[i].event_handle;
            break;
        }
    }
    portEXIT_CRITICAL(&event_lock);

    assert(event_handle != NULL);

    return xEventGroupClearBits(event_handle, bit_mask);
}

int esp_amp_event_bind_handle(uint16_t sysinfo_id, void *event_handle)
{
    if (event_handle == NULL) {
        return -1;
    }

    uint16_t event_bits_size = 0;
    atomic_uint *event_bits = esp_amp_sys_info_get(sysinfo_id, &event_bits_size, SYS_INFO_CAP_HP);
    assert(event_bits != NULL && event_bits_size == sizeof(atomic_uint));

    int idx_dup = ESP_AMP_EVENT_TABLE_LEN;
    int idx_free = ESP_AMP_EVENT_TABLE_LEN;

    portENTER_CRITICAL(&event_lock);
    for (int i = 0; i < ESP_AMP_EVENT_TABLE_LEN; i++) {
        if (event_table[i].event_handle == NULL && idx_free == ESP_AMP_EVENT_TABLE_LEN) {
            idx_free = i;
        }
        if (event_table[i].sysinfo_id == sysinfo_id && idx_dup == ESP_AMP_EVENT_TABLE_LEN) {
            idx_dup = i;
        }
    }
    /* duplicated entry has priority over free entry */
    int idx_insert = (idx_dup == ESP_AMP_EVENT_TABLE_LEN) ? idx_free : idx_dup;
    if (idx_insert != ESP_AMP_EVENT_TABLE_LEN) {
        event_table[idx_insert].sysinfo_id = sysinfo_id;
        event_table[idx_insert].event_handle = event_handle;
        event_table[idx_insert].event_bits = event_bits;
    }
    portEXIT_CRITICAL(&event_lock);

    /* no free entry or duplicated entry to insert */
    if (idx_dup == ESP_AMP_EVENT_TABLE_LEN && idx_free == ESP_AMP_EVENT_TABLE_LEN) {
        return -1;
    }

    /* set bits once bound to avoid missing event*/
    uint32_t unprocessed = atomic_load(event_bits);
    while (!atomic_compare_exchange_weak(event_bits, &unprocessed, 0))
        ;
    xEventGroupSetBits(event_handle, unprocessed);
    ESP_AMP_LOGD(TAG, "event_bits(%04x) bind value %p", sysinfo_id, (void *)unprocessed);
    return 0;
}

void esp_amp_event_unbind_handle(uint16_t sysinfo_id)
{
    portENTER_CRITICAL(&event_lock);
    for (int i = 0; i < ESP_AMP_EVENT_TABLE_LEN; i++) {
        if (event_table[i].sysinfo_id == sysinfo_id) {
            event_table[i].sysinfo_id = 0;
            event_table[i].event_handle = NULL;
            event_table[i].event_bits = NULL;
            break;
        }
    }
    portEXIT_CRITICAL(&event_lock);
}

void esp_amp_event_table_dump(void)
{
    ESP_AMP_LOGI("", "==== EVENT TABLE[%d] ====", ESP_AMP_EVENT_TABLE_LEN);
    ESP_AMP_LOGI("", "ID\t\tHANDLE");
    for (int i = 0; i < ESP_AMP_EVENT_TABLE_LEN; i++) {
        if (event_table[i].event_handle) {
            ESP_AMP_LOGI("", "0x%08x\t%p", event_table[i].sysinfo_id, event_table[i].event_handle);
        }
    }
    ESP_AMP_LOGI("", "END\n");
}

#if IS_MAIN_CORE
int esp_amp_event_create(uint16_t sysinfo_id)
{
    atomic_int *event_bits = esp_amp_sys_info_alloc(sysinfo_id, sizeof(atomic_int), SYS_INFO_CAP_HP);
    if (event_bits == NULL) {
        return -1;
    }
    atomic_init(event_bits, 0);
    return 0;
}
#endif /* IS_MAIN_CORE */

int esp_amp_event_init(void)
{
#if IS_MAIN_CORE
    assert(esp_amp_event_create(SYS_INFO_RESERVED_ID_EVENT_MAIN) == 0);
    assert(esp_amp_event_create(SYS_INFO_RESERVED_ID_EVENT_SUB) == 0);
#else
    uint16_t event_bits_size = 0;
    atomic_uint *main_core_event_bits =
        (atomic_uint *)esp_amp_sys_info_get(SYS_INFO_RESERVED_ID_EVENT_MAIN, &event_bits_size);
    assert(main_core_event_bits != NULL && event_bits_size == sizeof(atomic_uint));
    atomic_uint *sub_core_event_bits =
        (atomic_uint *)esp_amp_sys_info_get(SYS_INFO_RESERVED_ID_EVENT_SUB, &event_bits_size);
    assert(sub_core_event_bits != NULL && event_bits_size == sizeof(atomic_uint));
#endif /* IS_MAIN_CORE */

    /* init event group table */
    portENTER_CRITICAL(&event_lock);
    memset(event_table, 0, sizeof(event_table));
    portEXIT_CRITICAL(&event_lock);

    EventGroupHandle_t default_event_handle = xEventGroupCreateStatic(&default_event_storage);
    assert(default_event_handle != NULL);

#if IS_MAIN_CORE
    esp_amp_event_bind_handle(SYS_INFO_RESERVED_ID_EVENT_SUB, default_event_handle);
#else
    esp_amp_event_bind_handle(SYS_INFO_RESERVED_ID_EVENT_MAIN, default_event_handle);
#endif /* IS_MAIN_CORE */

    /* register sw interrupt */
    assert(esp_amp_sw_intr_add_handler(SW_INTR_RESERVED_ID_EVENT, os_env_event_isr, NULL) == 0);
    return 0;
}
