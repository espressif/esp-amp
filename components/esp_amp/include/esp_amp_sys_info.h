/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYS_INFO_RESERVED_ID = 0xff00,
    SYS_INFO_RESERVED_ID_EVENT_MAIN, /* reserved for main core event */
    SYS_INFO_RESERVED_ID_EVENT_SUB,  /* reserved for sub core event */
    SYS_INFO_RESERVED_ID_VQUEUE,     /* store shared queue (packed virt queue) data structure and buffer */
    SYS_INFO_RESERVED_ID_SYSTEM,     /* reserved for system service */
    SYS_INFO_RESERVED_ID_LSCTRL,     /* reserved for light sleep control */
    SYS_INFO_ID_MAX = 0xffff,        /* max number of sys info */
} esp_amp_sys_info_id_t;

typedef enum {
    SYS_INFO_CAP_HP = 0,
    SYS_INFO_CAP_RTC = 1,
} esp_amp_sys_info_cap_t;

/**
 * @brief Allocate sys info
 *
 * This API is intended for maincore to create necessary background information for dual-core communication
 *
 * @param info_id identifier for sys info data (0x0000 ~ 0xff00). 0xff00 ~ 0xffff is reserved for internal use
 * @param size size of sys info data needed
 *
 * @retval NULL failed to alloc sys info
 * @retval pointer to allocated shared memory region for sys info data
 */
void *esp_amp_sys_info_alloc(uint16_t info_id, uint16_t size, esp_amp_sys_info_cap_t cap);

/**
 * @brief Get sys info
 *
 * This API is intended for subcore to get background information created by maincore
 *
 * @param info_id identifier for sys info data (0x0000 ~ 0xff00). 0xff00 ~ 0xffff is reserved for internal use
 * @param size size of sys info data allocated by maincore
 *
 * @retval NULL failed to get sys info
 * @retval pointer to allocated shared memory region for sys info data
 */
void *esp_amp_sys_info_get(uint16_t info_id, uint16_t *size, esp_amp_sys_info_cap_t cap);

/**
 * Init sys info manager
 *
 * @retval 0 if successful
 * @retval failed to init sys info manager
 */
int esp_amp_sys_info_init(void);

/**
 * Dump sys info data (for debug use)
 */
void esp_amp_sys_info_dump(void);

#ifdef __cplusplus
}
#endif
