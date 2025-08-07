/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "esp_amp.h"
#include "esp_bit_defs.h"

#define EVENT_SUBCORE_READY (BIT0 | BIT1 | BIT2 | BIT3)

int main(void)
{
    assert(esp_amp_init() == 0);

    esp_amp_event_notify(EVENT_SUBCORE_READY);

    esp_amp_platform_delay_ms(2000);

    esp_amp_event_notify(EVENT_SUBCORE_READY);

    while (1)
        ;
}
