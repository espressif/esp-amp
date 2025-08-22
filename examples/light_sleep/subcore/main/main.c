/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "esp_amp.h"
#include "esp_amp_platform.h"
#include "esp_amp_pm.h"

#include "event.h"

#include "add.h"    /* NOTE: libadd.a is in RTC RAM, see extra.lf */
#include "myrand.h" /* NOTE: libmyrand.a is in HP RAM, see extra.lf */

static esp_amp_rpmsg_dev_t rpmsg_dev;
static esp_amp_rpmsg_ept_t ept0;

static volatile bool echoed_ready = false;
static volatile int echoed_value = 0;

static int ept0_cb(void *msg_data, uint16_t data_len, uint16_t src_addr, void *rx_cb_data)
{
    (void)data_len;
    (void)src_addr;
    (void)rx_cb_data;

    // Copy echoed int then release buffer
    int val = *((int *)msg_data);
    echoed_value = val;
    esp_amp_platform_memory_barrier();
    echoed_ready = true;

    esp_amp_rpmsg_destroy(&rpmsg_dev, msg_data);
    return 0;
}

int main(void)
{
    printf("SUB: start\n");

    assert(esp_amp_init() == 0);
    assert(esp_amp_rpmsg_sub_init(&rpmsg_dev, true, true) == 0);

    esp_amp_rpmsg_create_endpoint(&rpmsg_dev, 0, ept0_cb, NULL, &ept0);

    /* WARN: event APIs are NOT safe to use during maincore light sleep */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();
    esp_amp_event_notify(EVENT_SUBCORE_READY);
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();

    /* NOTE: use myrand() from component myrand (should skip sleep) */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();
    seed(esp_amp_platform_get_time_ms());
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();

    for (;;) {
        while (esp_amp_rpmsg_poll(&rpmsg_dev) == 0) {
            break;
        }

        /* NOTE: use myrand() from component myrand (should skip sleep) */
        ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();
        int a = myrand() % 1000;
        int b = myrand() % 1000;
        ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();

        /* give maincore sometime to enter light sleep */
        esp_amp_platform_delay_ms(500);

        /* NOTE: use add() from component add (no need to skip sleep) */
        int s = add(a, b);

        /*
         * NOTE: software interrupt and queue APIs are safe to use during maincore light sleep
         *       hence, rpmsg, which uses them under the hood, is also unaffected by maincore light sleep
         */
        int *msg = (int *)esp_amp_rpmsg_create_message(&rpmsg_dev, sizeof(int), ESP_AMP_RPMSG_DATA_DEFAULT);
        if (!msg) {
            printf("SUB: alloc failed, retry\n");
            esp_amp_platform_delay_ms(50);
            continue;
        }
        *msg = add(a, b);
        assert(esp_amp_rpmsg_send_nocopy(&rpmsg_dev, &ept0, 0, msg, sizeof(int)) == 0);
        printf("SUB: sent sum=%d (a=%d, b=%d), waiting echo\n", s, a, b);

        /* wait for echo: poll until callback sets echoed_ready */
        echoed_ready = false;
        while (!echoed_ready) {
            (void)esp_amp_rpmsg_poll(&rpmsg_dev);
            esp_amp_platform_delay_ms(1);
        }

        /* validation */
        printf("SUB: recieved echoed sum=%d\n", echoed_value);
        assert(echoed_value == s);

        esp_amp_platform_delay_ms(500);
    }

    return 0;
}
