/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C6
#define ESP_AMP_SHARED_MEM_END 0x4087e560
#elif CONFIG_IDF_TARGET_ESP32C5
#define ESP_AMP_SHARED_MEM_END 0x4085e4f0
#elif CONFIG_IDF_TARGET_ESP32P4
#define ESP_AMP_SHARED_MEM_END 0x4ff7f000
#endif

#define ALIGN_DOWN(size, align) ((size) & ~((align) - 1))

/* shared memory region */
#if CONFIG_ESP_AMP_SHARED_MEM_IN_HP
#define ESP_AMP_SHARED_MEM_START ALIGN_DOWN(ESP_AMP_SHARED_MEM_END - CONFIG_ESP_AMP_SHARED_MEM_SIZE, 0x10)
#else
#error "CONFIG_ESP_AMP_SHARED_MEM_IN_LP is not supported yet"
#endif /* ESP_AMP_SHARED_MEM_IN_HP */

/* reserved shared memory region */
#define ESP_AMP_RESERVED_SHARED_MEM_SIZE 0x20

/* software interrupt bit */
#define ESP_AMP_SW_INTR_BIT_ADDR ESP_AMP_SHARED_MEM_START

/* sys info or customized shared memory pool */
#if CONFIG_ESP_AMP_SHARED_MEM_IN_HP
#define ESP_AMP_SHARED_MEM_POOL_START (ESP_AMP_SHARED_MEM_START + ESP_AMP_RESERVED_SHARED_MEM_SIZE)
#else
#error "CONFIG_ESP_AMP_SHARED_MEM_IN_LP is not supported yet"
#endif
#define ESP_AMP_SHARED_MEM_POOL_SIZE (ESP_AMP_SHARED_MEM_END - ESP_AMP_SHARED_MEM_POOL_START)

/* hp memory for subcore use */
#if CONFIG_ESP_AMP_SUBCORE_BUILD_TYPE_PURE_RTC_RAM_APP
#define SUBCORE_USE_HP_MEM_END ESP_AMP_HP_SHARED_MEM_START
#define SUBCORE_USE_HP_MEM_SIZE CONFIG_ESP_AMP_SUBCORE_USE_HP_MEM_SIZE
#define SUBCORE_USE_HP_MEM_START (SUBCORE_USE_HP_MEM_END - SUBCORE_USE_HP_MEM_SIZE)
#endif
