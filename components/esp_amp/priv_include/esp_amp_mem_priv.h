/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C6
#define ESP_AMP_HP_SHARED_MEM_END 0x4087e560
#elif CONFIG_IDF_TARGET_ESP32C5
#define ESP_AMP_HP_SHARED_MEM_END 0x4085e4f0
#elif CONFIG_IDF_TARGET_ESP32P4
#define ESP_AMP_HP_SHARED_MEM_END 0x4ff7f000
#endif

#define ALIGN_DOWN(size, align) ((size) & ~((align) - 1))
#ifndef ALIGN_UP /* to prevent collision with esp_system/ld/ld.common */
#define ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))
#endif /* ALIGN_UP */

/* shared memory region (HP RAM) */
#define ESP_AMP_HP_SHARED_MEM_START ALIGN_DOWN(ESP_AMP_HP_SHARED_MEM_END - CONFIG_ESP_AMP_HP_SHARED_MEM_SIZE, 0x10)

/* reserved shared memory region */
#define ESP_AMP_HP_RESERVED_SHARED_MEM_SIZE 0x20

/* software interrupt bit */
#define ESP_AMP_SW_INTR_BIT_ADDR ESP_AMP_HP_SHARED_MEM_START

/* sys info or customized shared memory pool */
#define ESP_AMP_HP_SHARED_MEM_POOL_START (ESP_AMP_HP_SHARED_MEM_START + ESP_AMP_HP_RESERVED_SHARED_MEM_SIZE)
#define ESP_AMP_HP_SHARED_MEM_POOL_SIZE (ESP_AMP_HP_SHARED_MEM_END - ESP_AMP_HP_SHARED_MEM_POOL_START)

#if CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE
#if CONFIG_ESP_ROM_HAS_LP_ROM
#define RTC_MEM_START (0x50000000 + RESERVE_RTC_MEM)
#else /* !CONFIG_ESP_ROM_HAS_LP_ROM */
#define RTC_MEM_START (0x50000000)
#endif /* CONFIG_ESP_ROM_HAS_LP_ROM */

/*
 * Ensure the end where the shared memory starts is aligned to 8 bytes
 * if updating this also update the same in ulp_lp_core_memory_shared.c
 */
#define RTC_MEM_SIZE ALIGN_DOWN(CONFIG_ULP_COPROC_RESERVE_MEM, 0x8)

/* shared memory region (RTC RAM) */
#define ESP_AMP_RTC_SHARED_MEM_POOL_SIZE ALIGN_UP(CONFIG_ESP_AMP_RTC_SHARED_MEM_SIZE, 0x10)
#define ESP_AMP_RTC_SHARED_MEM_POOL_START (RTC_MEM_START + RTC_MEM_SIZE - ESP_AMP_RTC_SHARED_MEM_POOL_SIZE)
#endif /* CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE */

/* hp memory for subcore use */
#if CONFIG_ESP_AMP_SUBCORE_BUILD_TYPE_PURE_RTC_RAM_APP
#define SUBCORE_USE_HP_MEM_END ESP_AMP_HP_SHARED_MEM_START
#define SUBCORE_USE_HP_MEM_SIZE CONFIG_ESP_AMP_SUBCORE_USE_HP_MEM_SIZE
#define SUBCORE_USE_HP_MEM_START (SUBCORE_USE_HP_MEM_END - SUBCORE_USE_HP_MEM_SIZE)
#endif
