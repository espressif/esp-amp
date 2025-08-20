/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "string.h"
#include "riscv/rvruntime-frames.h"
#include "riscv/csr.h"
#include "esp_amp_sw_intr.h"
#include "esp_amp_panic.h"
#include "esp_amp_panic_priv.h"

#define ASSERT_STR "assert failed on subcore: "
#define INST_LEN 4

/**
 * @brief Convert integer to decimal string
 * @param value Integer value to convert (0-9999)
 * @param str Output buffer
 * @param max_len Maximum length of buffer (including null terminator)
 */
static inline void itods(int value, char* str, int max_len)
{
    // Initialize all positions to null
    for (int i = 0; i < max_len; i++) {
        str[i] = '\0';
    }

    // Handle zero case
    if (value == 0) {
        str[0] = '0';
        return;
    }

    // Fill digits backwards from the rightmost position
    int pos = 0;
    while (value > 0 && pos < max_len - 1) {
        str[pos] = '0' + (value % 10);
        value /= 10;
        pos++;
    }

    // Reverse the digits in place
    for (int i = 0; i < pos / 2; i++) {
        char temp = str[i];
        str[i] = str[pos - 1 - i];
        str[pos - 1 - i] = temp;
    }
}

/**
 * @brief Convert integer to hex string (8 digits, zero-padded)
 * @param value 32-bit value to convert
 * @param str Output buffer (must be at least 9 bytes for 8 digits + null terminator)
 */
static inline void itohs(uint32_t value, char* str)
{
    static const char hex_digits[] = "0123456789abcdef";

    // Generate 8 hex digits, working from right to left
    for (int i = 7; i >= 0; i--) {
        str[i] = hex_digits[value & 0xF];
        value >>= 4;
    }
    str[8] = '\0';
}

void panic_handler(RvExcFrame *frame, int exccause)
__attribute__((alias("subcore_panic_dump")));

void subcore_panic_dump(RvExcFrame *frame, int exccause)
{
    /* dump registers */
    char *reg_dump = (char *)g_esp_amp_subcore_panic_dump->register_dump;
    memcpy(reg_dump, (void *)frame, sizeof(RvExcFrame));

#if CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE
    /* dump extra registers */
    static RvExtraExcFrame exframe;
    exframe.ldpc0    = RV_READ_CSR(LDPC0);
    exframe.ldtval0  = RV_READ_CSR(LDTVAL0);
    exframe.ldpc1    = RV_READ_CSR(LDPC1);
    exframe.ldtval1  = RV_READ_CSR(LDTVAL1);
    exframe.stpc0    = RV_READ_CSR(STPC0);
    exframe.sttval0  = RV_READ_CSR(STTVAL0);
    exframe.stpc1    = RV_READ_CSR(STPC1);
    exframe.sttval1  = RV_READ_CSR(STTVAL1);
    exframe.stpc2    = RV_READ_CSR(STPC2);
    exframe.sttval2  = RV_READ_CSR(STTVAL2);

    memcpy(reg_dump + sizeof(RvExcFrame), (void *)&exframe, sizeof(RvExtraExcFrame));
#endif

    /* dump stack data */
    extern int __stack_top;
    char *stack_dump = (char *)g_esp_amp_subcore_panic_dump->stack_dump;
    g_esp_amp_subcore_panic_dump->stack_dump_length = MIN(ESP_AMP_PANIC_STACK_DUMP_LENGTH, (uint32_t)&__stack_top - (uint32_t)frame->sp);
    memcpy(stack_dump, (void *)frame->sp, g_esp_amp_subcore_panic_dump->stack_dump_length);

#if CONFIG_ESP_AMP_SYSTEM_ENABLE_SUPPLICANT
    /* trigger panic interrupt manually */
    esp_amp_sw_intr_trigger(SW_INTR_RESERVED_ID_PANIC);
#else
    subcore_panic_print();
#endif

    while (1);
}

void xt_unhandled_exception(RvExcFrame *frame)
{
    subcore_panic_dump(frame, frame->mcause);
}

void panicHandler(RvExcFrame *frame)
{
    subcore_panic_dump(frame, frame->mcause);
}

void __attribute__((noreturn)) __assert_func(const char *file, int line, const char *func, const char *expr)
{
#if CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT
    (void)file;
    (void)line;
    (void)func;
    (void)expr;

    char buff[sizeof(ASSERT_STR) + 11] = ASSERT_STR;
    buff[sizeof(ASSERT_STR) - 1] = '0';
    buff[sizeof(ASSERT_STR)] = 'x';
    itohs((uint32_t)(__builtin_return_address(0) - INST_LEN), buff + sizeof(ASSERT_STR) + 1);
    memcpy(g_esp_amp_subcore_panic_dump->abort_details, buff, sizeof(buff));
    g_esp_amp_subcore_panic_dump->abort_details[sizeof(buff) - 1] = '\0';
#else
    char lbuf[6];
    uint32_t rem_len = ESP_AMP_PANIC_ABORT_DETAILS_LENGTH - 1;
    uint32_t off = 0;

    itods(line, lbuf, 6);

    const char *str[] = {"assert failed on subcore: ", func ? func : "\b", " ", file, ":", lbuf, " (", expr, ")"};

    for (int i = 0; i < sizeof(str) / sizeof(str[0]); i++) {
        uint32_t len = strlen(str[i]);
        uint32_t cpy_len = MIN(len, rem_len);
        memcpy(g_esp_amp_subcore_panic_dump->abort_details + off, str[i], cpy_len);
        rem_len -= cpy_len;
        off += cpy_len;
        if (rem_len == 0) {
            break;
        }
    }

    g_esp_amp_subcore_panic_dump->abort_details[off] = '\0';
#endif
    g_esp_amp_subcore_panic_dump->is_abort = true;

    /* trigger exception */
    asm("unimp");
    while (1);
}

/**
 * @note for lp core target, abort() is defined in lp_core_utils.c, which simply put lp core into busy loop.
 * We wrap it here to add abort message.
 */
void __attribute__((noreturn)) esp_amp_subcore_abort(void)
{
#define ERR_STR1  "abort() was called at PC 0x"
#define ERR_STR2  " on subcore"

    char addr_buf[9] = { 0 };
    uint32_t rem_len = ESP_AMP_PANIC_ABORT_DETAILS_LENGTH - 1;
    uint32_t off = 0;

    itohs((uint32_t)(__builtin_return_address(0) - 4), addr_buf);
    const char *str[] = { ERR_STR1, addr_buf, ERR_STR2 };

    for (int i = 0; i < sizeof(str) / sizeof(str[0]); i++) {
        uint32_t len = strlen(str[i]);
        uint32_t cpy_len = MIN(len, rem_len);
        memcpy(g_esp_amp_subcore_panic_dump->abort_details + off, str[i], cpy_len);
        rem_len -= cpy_len;
        off += cpy_len;
        if (rem_len == 0) {
            break;
        }
    }
    g_esp_amp_subcore_panic_dump->abort_details[off] = '\0';
    g_esp_amp_subcore_panic_dump->is_abort = true;

    /* trigger exception */
    asm("unimp");
    while (1);
}
