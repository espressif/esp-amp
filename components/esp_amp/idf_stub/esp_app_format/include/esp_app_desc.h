/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "esp_assert.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ESP_APP_DESC_MAGIC_WORD (0xABCD5432)  /*!< The magic word for the esp_app_desc structure that is in DROM. */

/**
 * @brief Description about application.
 */
typedef struct {
    uint32_t magic_word;        /*!< Magic word ESP_APP_DESC_MAGIC_WORD */
    uint32_t secure_version;    /*!< Secure version */
    uint32_t reserv1[2];        /*!< reserv1 */
    char version[32];           /*!< Application version */
    char project_name[32];      /*!< Project name */
    char time[16];              /*!< Compile time */
    char date[16];              /*!< Compile date*/
    char idf_ver[32];           /*!< Version IDF */
    uint8_t app_elf_sha256[32]; /*!< sha256 of elf file */
    uint16_t min_efuse_blk_rev_full; /*!< Minimal eFuse block revision supported by image, in format: major * 100 + minor */
    uint16_t max_efuse_blk_rev_full; /*!< Maximal eFuse block revision supported by image, in format: major * 100 + minor */
    uint8_t mmu_page_size;      /*!< MMU page size in log base 2 format */
    uint8_t reserv3[3];         /*!< reserv3 */
    uint32_t reserv2[18];       /*!< reserv2 */
} esp_app_desc_t;

/** @cond */
ESP_STATIC_ASSERT(sizeof(esp_app_desc_t) == 256, "esp_app_desc_t should be 256 bytes");
ESP_STATIC_ASSERT(offsetof(esp_app_desc_t, secure_version) == 4, "secure_version field must be at 4 offset");
/** @endcond */

#ifdef __cplusplus
}
#endif
