/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "sdkconfig.h"
#include <stdint.h>
#include <stdio.h>
#include "esp_amp_platform.h"
#include "esp_amp.h"
#include "greeting.h"

#include "event.h"

int main(void)
{
    /* greeting() defined in components/sub_greeting */
    greeting();
    assert(esp_amp_init() == 0);
    esp_amp_event_notify(EVENT_SUBCORE_READY);
    abort();
    while(1) {
        printf("SUB: Running...\r\n");
        /* CONFIG_SUBCORE_MAIN_DELAY_MS defined in components/sub_main_cfg */
        esp_amp_platform_delay_ms(CONFIG_SUBCORE_MAIN_DELAY_MS);
    }
    return 0;
}
