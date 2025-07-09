/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "stdint.h"
#include "esp_amp_mem_priv.h"
#include "riscv/rvruntime-frames.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE
#define LDTVAL0         0xBE8
#define LDTVAL1         0xBE9
#define STTVAL0         0xBF8
#define STTVAL1         0xBF9
#define STTVAL2         0xBFA

STRUCT_BEGIN
STRUCT_FIELD(long, 4, RV_BUS_LDPC0,   ldpc0)           /* Load bus error PC register 0 */
STRUCT_FIELD(long, 4, RV_BUS_LDTVAL0, ldtval0)         /* Load bus error access address register 0 */
STRUCT_FIELD(long, 4, RV_BUS_LDPC1,   ldpc1)           /* Load bus error PC register 1 */
STRUCT_FIELD(long, 4, RV_BUS_LDTVAL1, ldtval1)         /* Load bus error access address register 1 */
STRUCT_FIELD(long, 4, RV_BUS_STPC0,   stpc0)           /* Store bus error PC register 0 */
STRUCT_FIELD(long, 4, RV_BUS_STTVAL0, sttval0)         /* Store bus error access address register 0 */
STRUCT_FIELD(long, 4, RV_BUS_STPC1,   stpc1)           /* Store bus error PC register 1 */
STRUCT_FIELD(long, 4, RV_BUS_STTVAL1, sttval1)         /* Store bus error access address register 1*/
STRUCT_FIELD(long, 4, RV_BUS_STPC2,   stpc2)           /* Store bus error PC register 2 */
STRUCT_FIELD(long, 4, RV_BUS_STTVAL2, sttval2)         /* Store bus error access address register 2*/
STRUCT_END(RvExtraExcFrame)
#endif /* CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE */

#ifdef __cplusplus
}
#endif
