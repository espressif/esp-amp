/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#include "sdkconfig.h"
#include "soc/soc.h"
#include "esp_app_desc.h"
#include "hal/misc.h"
#include "esp_err.h"
#include "esp_assert.h"
#include "esp_partition.h"
#include "esp_image_format.h"
#include "esp_heap_caps_init.h"
#include "spi_flash_mmap.h"
#include "heap_memory_layout.h"

#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
#include "ulp_lp_core.h"
#include "ulp_lp_core_memory_shared.h"
#include "ulp_lp_core_lp_timer_shared.h"
#endif /* CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE */

#include "esp_amp.h"
#include "esp_amp_log.h"
#include "esp_amp_system.h"
#include "esp_amp_mem_priv.h"

#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
#if ESP_ROM_HAS_LP_ROM
extern uint32_t _rtc_ulp_memory_start;
static uint32_t *ulp_base_address = (uint32_t *) &_rtc_ulp_memory_start;
#else
static uint32_t *ulp_base_address = RTC_SLOW_MEM;
#endif

#define ULP_RESET_HANDLER_ADDR (intptr_t)(ulp_base_address + 0x80 / 4)
#endif

const static char *TAG = "esp-amp-loader";

#if !CONFIG_ESP_AMP_SUBCORE_BUILD_TYPE_PURE_RTC_RAM_APP
SOC_RESERVE_MEMORY_REGION((intptr_t)(SUBCORE_USE_HP_MEM_START), (intptr_t)(SUBCORE_USE_HP_MEM_END), subcore_use);

static inline bool is_valid_subcore_app_dram_addr(intptr_t addr)
{
    intptr_t dram_reserved_start = SUBCORE_USE_HP_MEM_START;
    intptr_t dram_reserved_end = SUBCORE_USE_HP_MEM_END;
    return (addr >= dram_reserved_start && addr <= dram_reserved_end);
}
#endif

#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
static inline bool is_valid_subcore_app_rtcram_addr(intptr_t addr)
{
    intptr_t rtc_ram_reserved_start = (intptr_t)(ulp_base_address);
    intptr_t rtc_ram_reserved_end = rtc_ram_reserved_start + ALIGN_DOWN(CONFIG_ULP_COPROC_RESERVE_MEM, 0x8);
    return (addr >= rtc_ram_reserved_start && addr <= rtc_ram_reserved_end);
}
#endif

static bool is_valid_subcore_app_addr(intptr_t addr)
{
    bool ret = false;
#if !CONFIG_ESP_AMP_SUBCORE_BUILD_TYPE_PURE_RTC_RAM_APP
    ret |= is_valid_subcore_app_dram_addr(addr);
#endif

#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
    ret |= is_valid_subcore_app_rtcram_addr(addr);
#endif
    return ret;
}

static void show_sub_app_info(esp_app_desc_t *app_desc)
{
    ESP_AMP_LOGI(TAG, "Subcore App Info:");
    if (app_desc->project_name[0]) {
        ESP_AMP_LOGI(TAG, "Project name:     %s", app_desc->project_name);
    }
    if (app_desc->version[0]) {
        ESP_AMP_LOGI(TAG, "App version:      %s", app_desc->version);
    }
    ESP_AMP_LOGI(TAG, "Secure version:   %" PRIu32, app_desc->secure_version);
    if (app_desc->date[0]) {
        ESP_AMP_LOGI(TAG, "Compile time:     %s %s", app_desc->date, app_desc->time);
    }
    ESP_AMP_LOGI(TAG, "ESP-IDF:          %s", app_desc->idf_ver);
}

esp_err_t esp_amp_load_sub_from_partition(const esp_partition_t *sub_partition)
{
    esp_partition_mmap_handle_t handle;
    const void *sub_partition_ptr;

    if (sub_partition == NULL) {
        ESP_AMP_LOGE(TAG, "subcore partition not found");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_ERROR_CHECK(
        esp_partition_mmap(sub_partition, 0, sub_partition->size, SPI_FLASH_MMAP_DATA, &sub_partition_ptr, &handle));
    ESP_ERROR_CHECK(esp_amp_load_sub(sub_partition_ptr));

    esp_partition_munmap(handle);
    return ESP_OK;
}

esp_err_t esp_amp_load_sub(const void *sub_bin)
{
    esp_err_t ret = ESP_OK;

    esp_image_metadata_t sub_img_data = {0};
    uint8_t *sub_bin_byte_ptr = (uint8_t *)(sub_bin);

#if !CONFIG_ESP_AMP_SUBCORE_BUILD_TYPE_PURE_RTC_RAM_APP
    ESP_AMP_LOGI(TAG, "Reserved dram region (%p - %p) for subcore", (void *)(SUBCORE_USE_HP_MEM_START),
                 (void *)SUBCORE_USE_HP_MEM_END);

    /* unused reserved dram to be given back to main-core heap */
    intptr_t unused_reserved_dram_start = SUBCORE_USE_HP_MEM_START;
#endif

    /* Turn off subcore before loading binary */
    esp_amp_stop_subcore();

#if !CONFIG_ESP_AMP_SUBCORE_BUILD_TYPE_PURE_RTC_RAM_APP
    hal_memset((void *)(SUBCORE_USE_HP_MEM_START), 0, SUBCORE_USE_HP_MEM_SIZE);
#endif

    memcpy(&sub_img_data.image, sub_bin_byte_ptr, sizeof(esp_image_header_t));

#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
    hal_memset(ulp_base_address, 0, CONFIG_ULP_COPROC_RESERVE_MEM);
    if (sub_img_data.image.entry_addr != ULP_RESET_HANDLER_ADDR) {
        ESP_AMP_LOGE(TAG, "Invalid entry address");
        ret = ESP_FAIL;
    }
#else
    if (!is_valid_subcore_app_addr(sub_img_data.image.entry_addr)) {
        ESP_AMP_LOGE(TAG, "Invalid entry address");
        ret = ESP_FAIL;
    }
#endif
    else {
        uint32_t next_addr = sizeof(esp_image_header_t);
        for (int i = 0; i < sub_img_data.image.segment_count; i++) {
            memcpy(&sub_img_data.segments[i], &sub_bin_byte_ptr[next_addr], sizeof(esp_image_segment_header_t));
            next_addr += sizeof(esp_image_segment_header_t);

            intptr_t segment_start = (intptr_t)sub_img_data.segments[i].load_addr;
            intptr_t segment_end = segment_start + sub_img_data.segments[i].data_len;

            if (is_valid_subcore_app_addr(segment_start) && is_valid_subcore_app_addr(segment_end)) {
                memcpy((void *)(sub_img_data.segments[i].load_addr), &sub_bin_byte_ptr[next_addr],
                       sub_img_data.segments[i].data_len);
                next_addr += sub_img_data.segments[i].data_len;

#if !CONFIG_ESP_AMP_SUBCORE_BUILD_TYPE_PURE_RTC_RAM_APP
                if (is_valid_subcore_app_dram_addr(segment_end) && (segment_end > unused_reserved_dram_start)) {
                    unused_reserved_dram_start = segment_end;
                }
#endif
            } else if (segment_start >= SOC_DROM_LOW && segment_end <= SOC_DROM_HIGH) {
                /* subcore app desc block. load address in rodata to make esptool happy. won't be loaded into ram */
                esp_app_desc_t *app_desc = (esp_app_desc_t *)(&sub_bin_byte_ptr[next_addr]);
                if (app_desc->magic_word == ESP_APP_DESC_MAGIC_WORD) {
                    show_sub_app_info(app_desc);
                    next_addr += sub_img_data.segments[i].data_len;
                } else {
                    ESP_AMP_LOGE(TAG, "Invalid app desc magic word");
                    ret = ESP_FAIL;
                    break;
                }
            } else {
                ESP_AMP_LOGE(TAG, "Invalid segment region (%p - %p)", (void *)segment_start, (void *)segment_end);
                ret = ESP_FAIL;
                break;
            }
        }
    }

#if !CONFIG_ESP_AMP_SUBCORE_BUILD_TYPE_PURE_RTC_RAM_APP
    /* give unused reserved dram region back to main-core heap */
    if (ret != ESP_OK) {
        unused_reserved_dram_start = SUBCORE_USE_HP_MEM_START;
    }

    if (heap_caps_add_region(unused_reserved_dram_start, (intptr_t)SUBCORE_USE_HP_MEM_END) == ESP_OK) {
        ESP_AMP_LOGI(TAG, "Give unused reserved dram region (%p - %p) back to main-core heap",
                     (void *)unused_reserved_dram_start, (void *)SUBCORE_USE_HP_MEM_END);
    }
#endif

#if CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE
    extern uint32_t hp_subcore_boot_addr;
    hp_subcore_boot_addr = sub_img_data.image.entry_addr;
#endif
    return ret;
}
