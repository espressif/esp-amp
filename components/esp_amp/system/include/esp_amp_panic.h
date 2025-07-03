/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "inttypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * esp amp panic dump struct
 *
 * @note this struct is a helper to organize the 4KB shared memory region for panic dump
 */
#define ESP_AMP_PANIC_ABORT_DETAILS_LENGTH 248 /* 248 + 8 = 256 bytes */
#define ESP_AMP_PANIC_REGISTER_DUMP_LENGTH 64 /* 64 registers,64 * 4 = 256 bytes */
#define ESP_AMP_PANIC_STACK_DUMP_LENGTH 1024 /* 1024 / 4 = 256 words */

typedef struct {
    uint32_t is_abort;
    uint32_t stack_dump_length;
    char abort_details[ESP_AMP_PANIC_ABORT_DETAILS_LENGTH];
    uint32_t register_dump[ESP_AMP_PANIC_REGISTER_DUMP_LENGTH];
    uint32_t stack_dump[ESP_AMP_PANIC_STACK_DUMP_LENGTH / sizeof(uint32_t)];
} esp_amp_subcore_panic_dump_t;

extern esp_amp_subcore_panic_dump_t *g_esp_amp_subcore_panic_dump;

void subcore_panic_print(void);

#ifdef __cplusplus
}
#endif
