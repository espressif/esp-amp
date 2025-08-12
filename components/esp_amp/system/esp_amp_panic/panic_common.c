/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "esp_amp_panic.h"
#include "esp_amp_panic_priv.h"
#include "esp_amp_sw_intr.h"
#include <stddef.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#if IS_MAIN_CORE
#include "esp_rom_uart.h"
#endif

esp_amp_subcore_panic_dump_t *g_esp_amp_subcore_panic_dump = (esp_amp_subcore_panic_dump_t *)ESP_AMP_HP_SHARED_MEM_END;

#define DIM(arr) (sizeof(arr)/sizeof(*arr))

static const char *desc[] = {
    "MEPC    ", "RA      ", "SP      ", "GP      ", "TP      ", "T0      ", "T1      ", "T2      ",
    "S0/FP   ", "S1      ", "A0      ", "A1      ", "A2      ", "A3      ", "A4      ", "A5      ",
    "A6      ", "A7      ", "S2      ", "S3      ", "S4      ", "S5      ", "S6      ", "S7      ",
    "S8      ", "S9      ", "S10     ", "S11     ", "T3      ", "T4      ", "T5      ", "T6      ",
    "MSTATUS ", "MTVEC   ", "MCAUSE  ", "MTVAL   ", "MHARTID "
};

#if CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE
static const char *extra_desc[] = {
    "LDPC0   ", "LDTVAL0 ", "LDPC1   ", "LDTVAL1 ", "STPC0   ", "STTVAL0 ", "STPC1   ", "STTVAL1 ",
    "STPC2   ", "STTVAL2 "
};
#endif /* CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE */

static const char *reason[] = {
#if CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE
    "Instruction address misaligned",
    "Instruction access fault",
#else
    NULL,
    NULL,
#endif /* CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE */
    "Illegal instruction",
    "Breakpoint",
    "Load address misaligned",
    "Load access fault",
    "Store address misaligned",
    "Store access fault",
#if CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE
    "Environment call from U-mode",
    "Environment call from S-mode",
    NULL,
    "Environment call from M-mode",
    "Instruction page fault",
    "Load page fault",
    NULL,
    "Store page fault",
#endif /* CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE */
};

static void subcore_panic_print_char(const char c)
{
#if IS_MAIN_CORE
    esp_rom_output_tx_one_char(c);
#else
    extern void subcore_uart_putchar(char c);
    subcore_uart_putchar(c);
#endif
}

static void subcore_panic_print_str(const char *str)
{
    for (int i = 0; str[i] != 0; i++) {
        subcore_panic_print_char(str[i]);
    }
}

static void subcore_panic_print_hex(int h)
{
    int x;
    int c;
    // Does not print '0x', only the digits (8 digits to print)
    for (x = 0; x < 8; x++) {
        c = (h >> 28) & 0xf; // extract the leftmost byte
        if (c < 10) {
            subcore_panic_print_char('0' + c);
        } else {
            subcore_panic_print_char('a' + c - 10);
        }
        h <<= 4; // move the 2nd leftmost byte to the left, to be extracted next
    }
}

static void subcore_panic_print_stack(void)
{
    RvExcFrame *frame = (RvExcFrame *)g_esp_amp_subcore_panic_dump->register_dump;

    uint32_t i = 0;
    uint32_t sp = frame->sp;
    uint32_t stack_dump_base = (uint32_t)g_esp_amp_subcore_panic_dump->stack_dump;
    uint32_t stack_dump_length = MIN(ESP_AMP_PANIC_STACK_DUMP_LENGTH, g_esp_amp_subcore_panic_dump->stack_dump_length);
    subcore_panic_print_str("\n\nStack memory:\n");
    const int per_line = 8;
    for (i = 0; i < stack_dump_length; i += per_line * sizeof(uint32_t)) {
        uint32_t *stack_dump_ptr = (uint32_t *)(stack_dump_base + i);
        subcore_panic_print_hex(sp + i);
        subcore_panic_print_str(": ");
        for (int y = 0; y < per_line; y++) {
            subcore_panic_print_str("0x");
            subcore_panic_print_hex(stack_dump_ptr[y]);
            subcore_panic_print_char(y == per_line - 1 ? '\n' : ' ');
        }
    }
    subcore_panic_print_str("\n");
}

#if CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE
static void subcore_panic_print_extra_regs(void)
{
    RvExtraExcFrame *exframe = (RvExtraExcFrame *)(g_esp_amp_subcore_panic_dump->register_dump + sizeof(RvExcFrame));

    subcore_panic_print_str("\n");
    uint32_t* frame_ints = (uint32_t*)(&exframe);
    for (int x = 0; x < DIM(extra_desc); x++) {
        if (extra_desc[x][0] != 0) {
            const int not_last = (x + 1) % 4;
            subcore_panic_print_str(extra_desc[x]);
            subcore_panic_print_str(": 0x");
            subcore_panic_print_hex(frame_ints[x]);
            subcore_panic_print_char(not_last ? ' ' : '\n');
        }
    }
}
#endif /* CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE */

void subcore_panic_print(void)
{
    RvExcFrame *frame = (RvExcFrame *)g_esp_amp_subcore_panic_dump->register_dump;
    uint32_t exccause = frame->mcause;

    const char *exccause_str = "Unhandled interrupt/Unknown cause";

    if (exccause < DIM(reason) && reason[exccause] != NULL) {
        exccause_str = reason[exccause];
    }

    if (g_esp_amp_subcore_panic_dump->is_abort) {
        subcore_panic_print_str(g_esp_amp_subcore_panic_dump->abort_details);
        subcore_panic_print_str("\n");
    } else {
        subcore_panic_print_str("Guru Meditation Error: ON Subcore panic'ed ");
        subcore_panic_print_str(exccause_str);
        subcore_panic_print_str("\n");
    }
    subcore_panic_print_str("Core 1 register dump:\n");

    uint32_t* frame_ints = (uint32_t*) frame;
    for (int x = 0; x < DIM(desc); x++) {
        if (desc[x][0] != 0) {
            const int not_last = (x + 1) % 4;
            subcore_panic_print_str(desc[x]);
            subcore_panic_print_str(": 0x");
            subcore_panic_print_hex(frame_ints[x]);
            subcore_panic_print_char(not_last ? ' ' : '\n');
        }
    }

#if CONFIG_ESP_AMP_SUBCORE_TYPE_HP_CORE
    subcore_panic_print_extra_regs();
#endif

    subcore_panic_print_stack();

    /* idf-monitor uses this string to mark the end of a panic dump */
    subcore_panic_print_str("ELF file SHA256: No SHA256 Embedded\n");
}
