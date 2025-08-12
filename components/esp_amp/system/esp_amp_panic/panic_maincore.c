/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "stdint.h"
#include "string.h"
#include "stdio.h"
#include "esp_log.h"
#include "esp_rom_uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heap_memory_layout.h"

#include "esp_amp_system.h"
#include "esp_amp_system_priv.h"
#include "esp_amp_mem_priv.h"
#include "esp_amp_sw_intr.h"
#include "esp_amp_panic.h"
#include "esp_amp_panic_priv.h"
#include "esp_amp_service.h"
#include "esp_amp_platform.h"

#define PANIC_DUMP_START_ADDR ESP_AMP_HP_SHARED_MEM_END
#define PANIC_DUMP_MAX_LEN   0x1000
#define PANIC_DUMP_END_ADDR (PANIC_DUMP_START_ADDR + PANIC_DUMP_MAX_LEN)

SOC_RESERVE_MEMORY_REGION((intptr_t)(PANIC_DUMP_START_ADDR), (intptr_t)(PANIC_DUMP_END_ADDR), subcore_panic);

/* subcore panic flag */
static bool s_is_subcore_panic = false;

static int mute_vprintf(const char *, va_list)
{
    return 0;
}

/**
 * @brief default panic handler for subcore
 */
void esp_amp_subcore_panic_handler(void)
__attribute__((weak, alias("esp_amp_subcore_panic_handler_default")));

void esp_amp_subcore_panic_handler_default(void)
{
    /* mute maincore log */
    vprintf_like_t orig_vprintf = esp_log_set_vprintf(mute_vprintf);
    esp_rom_install_channel_putc(1, NULL);

    /* flush output buffer */
    esp_rom_output_flush_tx(CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM);

    /* print subcore panic */
    subcore_panic_print();

    /* re-enable maincore log */
    esp_log_set_vprintf(orig_vprintf);
    esp_rom_install_uart_printf();
}

static IRAM_ATTR int subcore_panic_handler(void* arg)
{
    /* set subcore panic flag */
    s_is_subcore_panic = true;

    /* disable further interrupts */
    esp_amp_platform_sw_intr_disable();

    /* stop subcore immediately */
    esp_amp_stop_subcore();

    /* notify subcore supplicant */
    TaskHandle_t supplicant = (TaskHandle_t)esp_amp_system_get_supplicant();
    if (supplicant == NULL) {
        return 0;
    }

    int need_yield = 0;
    xTaskNotifyFromISR(supplicant, 0, eNoAction, &need_yield);
    portYIELD_FROM_ISR(need_yield);
    return 0;
}

bool esp_amp_subcore_panic(void)
{
    return s_is_subcore_panic;
}

int esp_amp_system_panic_init(void)
{
    return esp_amp_sw_intr_add_handler(SW_INTR_RESERVED_ID_PANIC, subcore_panic_handler, NULL);
}
