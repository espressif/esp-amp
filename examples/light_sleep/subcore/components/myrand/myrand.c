/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "myrand.h"
#include "esp_attr.h"

RTC_DATA_ATTR static int lcg_state = 1;

void seed(int s)
{
    if (s == 0) {
        s = 1;
    }
    lcg_state = s;
}

int myrand(void)
{
    lcg_state = (int)((16807ULL * lcg_state) % 2147483647ULL);
    return (int)lcg_state;
}
