/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_attr.h"

#if IS_MAIN_CORE
#include "heap_memory_layout.h"
#endif

#include "esp_amp_sys_info.h"
#include "esp_amp_log.h"
#include "esp_amp_mem_priv.h"
#include "esp_amp_system.h"

#define TAG "sys_info"

#define ESP_AMP_SYS_INFO_ID_MAX 0xffff

#if IS_MAIN_CORE
SOC_RESERVE_MEMORY_REGION(ESP_AMP_HP_SHARED_MEM_START, ESP_AMP_HP_SHARED_MEM_END, esp_amp_shared_mem);
#endif

typedef struct sys_info_header_t {
    uint16_t info_id;
    uint16_t size;                  /* original size in byte */
    struct sys_info_header_t *next; /* offset related to buffer */
} sys_info_header_t;

static sys_info_header_t *const s_esp_amp_sys_info_hp = (sys_info_header_t *)ESP_AMP_HP_SHARED_MEM_POOL_START;
#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
static sys_info_header_t *const s_esp_amp_sys_info_rtc = (sys_info_header_t *)ESP_AMP_RTC_SHARED_MEM_POOL_START;
#endif /* CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE */

#if IS_MAIN_CORE
static uint16_t get_size_word(uint16_t size)
{
    uint16_t size_in_word = size >> 2;
    if (size & 0x3) {
        size_in_word += 1;
    }
    return size_in_word;
}
#endif /* IS_MAIN_CORE */

void *IRAM_ATTR esp_amp_sys_info_get(uint16_t info_id, uint16_t *size, esp_amp_sys_info_cap_t cap)
{
    void *buffer = NULL;
#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
    sys_info_header_t *sys_info_entry =
        (cap == SYS_INFO_CAP_HP) ? s_esp_amp_sys_info_hp->next : s_esp_amp_sys_info_rtc->next;
#else
    if (cap == SYS_INFO_CAP_RTC) {
        goto exit;
    }
    sys_info_header_t *sys_info_entry = s_esp_amp_sys_info_hp->next;
#endif
    while (sys_info_entry != NULL) {
        if (sys_info_entry->info_id == info_id) {
            if (size != NULL) {
                *size = sys_info_entry->size;
            }

            buffer = (void *)((uint8_t *)(sys_info_entry) + sizeof(sys_info_header_t));
            ESP_AMP_LOGI(TAG, "get info:%x, size:0x%x, addr:%p", sys_info_entry->info_id, sys_info_entry->size, buffer);
            goto exit;
        }

        sys_info_entry = sys_info_entry->next;
    }

    ESP_AMP_LOGE(TAG, "INFO_ID(0x%x) not found", info_id);
exit:
    return buffer;
}

#if IS_MAIN_CORE
void *esp_amp_sys_info_alloc(uint16_t info_id, uint16_t size, esp_amp_sys_info_cap_t cap)
{
#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
    sys_info_header_t *sys_info_entry = (cap == SYS_INFO_CAP_HP) ? s_esp_amp_sys_info_hp : s_esp_amp_sys_info_rtc;
#else  /* CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE */
    if (cap == SYS_INFO_CAP_RTC) {
        return NULL;
    }
    sys_info_header_t *sys_info_entry = s_esp_amp_sys_info_hp;
#endif /* CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE */

    while (sys_info_entry->next != NULL) {
        if (sys_info_entry->next->info_id == info_id) {
            ESP_AMP_LOGE(TAG, "Info id(%x) already exist", info_id);
            return NULL;
        }
        sys_info_entry = sys_info_entry->next;
    }

    void *next_sys_info_entry_start =
        (uint8_t *)(sys_info_entry) + sizeof(sys_info_header_t) + 4 * get_size_word(sys_info_entry->size);
    void *next_sys_info_entry_end =
        (uint8_t *)(next_sys_info_entry_start) + sizeof(sys_info_header_t) + 4 * get_size_word(size);
    void *buffer = (void *)((uint8_t *)(next_sys_info_entry_start) + sizeof(sys_info_header_t));

#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
    void *shared_mem_end = (cap == SYS_INFO_CAP_HP)
                           ? (void *)(ESP_AMP_HP_SHARED_MEM_END)
                           : (void *)(ESP_AMP_RTC_SHARED_MEM_POOL_START + ESP_AMP_RTC_SHARED_MEM_POOL_SIZE);
#else
    void *shared_mem_end = (void *)(ESP_AMP_HP_SHARED_MEM_END);
#endif
    if (next_sys_info_entry_end > shared_mem_end) {
        ESP_AMP_LOGE(TAG, "No space in buffer");
        return NULL;
    }

    ESP_AMP_LOGI(TAG, "alloc info:%x, size:0x%x, addr:%p", info_id, size, buffer);

    sys_info_entry->next = next_sys_info_entry_start;
    sys_info_entry->next->next = NULL;
    sys_info_entry->next->info_id = info_id;
    sys_info_entry->next->size = size;

    return buffer;
}

#endif /* IS_MAIN_CORE */

int esp_amp_sys_info_init(void)
{
#if IS_MAIN_CORE
    s_esp_amp_sys_info_hp->info_id = ESP_AMP_SYS_INFO_ID_MAX;
    s_esp_amp_sys_info_hp->size = 0;
    s_esp_amp_sys_info_hp->next = NULL;
#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
    s_esp_amp_sys_info_rtc->info_id = ESP_AMP_SYS_INFO_ID_MAX;
    s_esp_amp_sys_info_rtc->size = 0;
    s_esp_amp_sys_info_rtc->next = NULL;
#endif /* CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE */
#endif /* IS_MAIN_CORE */
    ESP_AMP_LOGI(TAG, "ESP-AMP shared memory (HP RAM): addr=%p, len=%p", s_esp_amp_sys_info_hp,
                 (void *)ESP_AMP_HP_SHARED_MEM_POOL_SIZE);
    return 0;
    ESP_AMP_LOGI(TAG, "ESP-AMP shared memory (RTC RAM): addr=%p, len=%p", s_esp_amp_sys_info_rtc,
                 (void *)ESP_AMP_RTC_SHARED_MEM_POOL_SIZE);
}

void esp_amp_sys_info_dump(void)
{
    ESP_AMP_LOGI("", "====== SYS INFO(%p) ======", s_esp_amp_sys_info_hp);
    ESP_AMP_LOGI("", "ID\t\tSIZE\tADDR");
    sys_info_header_t *sys_info_entry = s_esp_amp_sys_info_hp->next;
    while (sys_info_entry != NULL) {
        ESP_AMP_LOGI("", "0x%08x\t0x%u\t%p", sys_info_entry->info_id, sys_info_entry->size,
                     (void *)((uint8_t *)(s_esp_amp_sys_info_hp) + sizeof(sys_info_header_t)));
        sys_info_entry = sys_info_entry->next;
    }
#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
    ESP_AMP_LOGI("", "====== SYS INFO(%p) ======", s_esp_amp_sys_info_rtc);
    ESP_AMP_LOGI("", "ID\t\tSIZE\tADDR");
    sys_info_entry = s_esp_amp_sys_info_rtc->next;
    while (sys_info_entry != NULL) {
        ESP_AMP_LOGI("", "0x%08x\t0x%u\t%p", sys_info_entry->info_id, sys_info_entry->size,
                     (void *)((uint8_t *)(s_esp_amp_sys_info_rtc) + sizeof(sys_info_header_t)));
        sys_info_entry = sys_info_entry->next;
    }
#endif
    ESP_AMP_LOGI("", "END\n");
}
