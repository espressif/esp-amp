/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_amp.h"

int main(void)
{
    assert(esp_amp_init() == 0);

    esp_amp_platform_delay_ms(1000);

    for (int i = 0; i < 16; i++) {
        esp_amp_sw_intr_trigger(SW_INTR_ID_0);
        esp_amp_platform_delay_ms(100);

        esp_amp_sw_intr_trigger(SW_INTR_ID_1);
        esp_amp_platform_delay_ms(100);

        esp_amp_sw_intr_trigger(SW_INTR_ID_2);
        esp_amp_platform_delay_ms(100);

        esp_amp_sw_intr_trigger(SW_INTR_ID_3);
        esp_amp_platform_delay_ms(100);
    }

    return 0;
}
