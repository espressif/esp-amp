/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "sdkconfig.h"
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int esp_amp_subcore_early_printf(const char *fmt, ...);
extern int64_t esp_amp_platform_get_time_ms(void);

typedef enum {
    ESP_LOG_NONE,       /*!< No log output */
    ESP_LOG_ERROR,      /*!< Critical errors, software module can not recover on its own */
    ESP_LOG_WARN,       /*!< Error conditions from which recovery measures have been taken */
    ESP_LOG_INFO,       /*!< Information messages which describe normal flow of events */
    ESP_LOG_DEBUG,      /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
    ESP_LOG_VERBOSE     /*!< Bigger chunks of debugging information, or frequent messages which can potentially flood the output. */
} esp_log_level_t;

#define AMP_LOG_LOCAL_LEVEL ( CONFIG_LOG_DEFAULT_LEVEL )

#if CONFIG_LOG_COLORS
#define LOG_COLOR_BLACK   "30"
#define LOG_COLOR_RED     "31"
#define LOG_COLOR_GREEN   "32"
#define LOG_COLOR_BROWN   "33"
#define LOG_COLOR_BLUE    "34"
#define LOG_COLOR_PURPLE  "35"
#define LOG_COLOR_CYAN    "36"
#define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define LOG_RESET_COLOR   "\033[0m"
#define LOG_COLOR_E       LOG_BOLD(LOG_COLOR_RED)
#define LOG_COLOR_W       LOG_BOLD(LOG_COLOR_BROWN)
#define LOG_COLOR_I       LOG_BOLD(LOG_COLOR_GREEN)
#define LOG_COLOR_D
#define LOG_COLOR_V
#else //CONFIG_LOG_COLORS
#define LOG_COLOR_E
#define LOG_COLOR_W
#define LOG_COLOR_I
#define LOG_COLOR_D
#define LOG_COLOR_V
#define LOG_RESET_COLOR
#endif //CONFIG_LOG_COLORS

#define LOG_FORMAT(letter, format)  LOG_COLOR_ ## letter #letter " (%" PRIu32 ") %s: " format LOG_RESET_COLOR "\r\n"

#define ESP_LOG_LEVEL(print_func, level, tag, format, ...) do { \
        if (level==ESP_LOG_ERROR )          { print_func(LOG_FORMAT(E, format), (uint32_t)esp_amp_platform_get_time_ms(), tag, ##__VA_ARGS__); } \
        else if (level==ESP_LOG_WARN )      { print_func(LOG_FORMAT(W, format), (uint32_t)esp_amp_platform_get_time_ms(), tag, ##__VA_ARGS__); } \
        else if (level==ESP_LOG_DEBUG )     { print_func(LOG_FORMAT(D, format), (uint32_t)esp_amp_platform_get_time_ms(), tag, ##__VA_ARGS__); } \
        else if (level==ESP_LOG_VERBOSE )   { print_func(LOG_FORMAT(V, format), (uint32_t)esp_amp_platform_get_time_ms(), tag, ##__VA_ARGS__); } \
        else                                { print_func(LOG_FORMAT(I, format), (uint32_t)esp_amp_platform_get_time_ms(), tag, ##__VA_ARGS__); } \
    } while(0)

#define ESP_LOG_LEVEL_LOCAL(print_func, level, tag, format, ...) do { \
        if ( AMP_LOG_LOCAL_LEVEL >= level ) ESP_LOG_LEVEL(print_func, level, tag, format, ##__VA_ARGS__); \
    } while(0)

#define ESP_LOGE( tag, format, ... )  ESP_LOG_LEVEL_LOCAL(printf, ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define ESP_LOGW( tag, format, ... )  ESP_LOG_LEVEL_LOCAL(printf, ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define ESP_LOGI( tag, format, ... )  ESP_LOG_LEVEL_LOCAL(printf, ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define ESP_LOGD( tag, format, ... )  ESP_LOG_LEVEL_LOCAL(printf, ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define ESP_LOGV( tag, format, ... )  ESP_LOG_LEVEL_LOCAL(printf, ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

/* print to UART directly */
#define ESP_EARLY_LOGE( tag, format, ... )  ESP_LOG_LEVEL_LOCAL(esp_amp_subcore_early_printf, ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGW( tag, format, ... )  ESP_LOG_LEVEL_LOCAL(esp_amp_subcore_early_printf, ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGI( tag, format, ... )  ESP_LOG_LEVEL_LOCAL(esp_amp_subcore_early_printf, ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGD( tag, format, ... )  ESP_LOG_LEVEL_LOCAL(esp_amp_subcore_early_printf, ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGV( tag, format, ... )  ESP_LOG_LEVEL_LOCAL(esp_amp_subcore_early_printf, ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

/* XIP is not supported */
#define ESP_DRAM_LOGE ESP_EARLY_LOGE
#define ESP_DRAM_LOGW ESP_EARLY_LOGW
#define ESP_DRAM_LOGI ESP_EARLY_LOGI
#define ESP_DRAM_LOGD ESP_EARLY_LOGD
#define ESP_DRAM_LOGV ESP_EARLY_LOGV

#define BYTES_PER_LINE 16

static inline void esp_log_buffer_hex_internal(const char *tag, const void *buffer, int buff_len,
                                               esp_log_level_t log_level)
{
    if (AMP_LOG_LOCAL_LEVEL < log_level) {
        return;
    }
    if (buff_len == 0) {
        return;
    }
    const char *p = (const char *)buffer;
    for (int i = 0; i < buff_len; i++) {
        if (i && !(i % BYTES_PER_LINE)) {
            printf("\n");
        }
        printf("%02X ", (*(p + i) & 0xff));
    }
    printf("\n");
}

#define ESP_LOG_BUFFER_HEXDUMP( tag, buffer, buff_len, level ) \
    do {\
            esp_log_buffer_hex_internal( tag, buffer, buff_len, level ); \
    } while(0)

#ifdef __cplusplus
}
#endif
