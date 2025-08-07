/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_amp.h"
#include "esp_amp_pm.h"

#define MAGIC_TAG 0x12345678
#define FROM_MAIN 1
#define FROM_SUB 2

typedef struct {
    int magic;
    int from;
    int idx;
} simple_msg_t;

static esp_amp_rpmsg_dev_t rpmsg_dev;
static esp_amp_rpmsg_ept_t ept0;

static int ept0_cb(void *msg_data, uint16_t data_len, uint16_t src_addr, void *rx_cb_data)
{
    (void)data_len;
    (void)src_addr;
    (void)rx_cb_data;
    esp_amp_rpmsg_destroy(&rpmsg_dev, msg_data);
    return 0;
}

int main(void)
{
    printf("HELLO!!\n");

    // init esp amp component */
    assert(esp_amp_init() == 0);

    /* init RPMsg transport */
    assert(esp_amp_rpmsg_sub_init(&rpmsg_dev, true, true) == 0);

    /* create endpoint 0 with simple RX validator */
    esp_amp_rpmsg_create_endpoint(&rpmsg_dev, 0, ept0_cb, NULL, &ept0);

    /* give maincore some time to goes to sleep */
    esp_amp_platform_delay_ms(1000);

    /* send 4 messages to main */
    for (int i = 0; i < 4; i++) {
        /* poll for incoming messages, this should not wakeup maincore */
        for (int j = 0; j < 100; j++) {
            (void)esp_amp_rpmsg_poll(&rpmsg_dev);
        }

        /* send */
        simple_msg_t *m =
            (simple_msg_t *)esp_amp_rpmsg_create_message(&rpmsg_dev, sizeof(simple_msg_t), ESP_AMP_RPMSG_DATA_DEFAULT);
        if (!m) {
            continue;
        }
        m->magic = MAGIC_TAG;
        m->from = FROM_SUB;
        m->idx = i;

        int ret = esp_amp_rpmsg_send_nocopy(&rpmsg_dev, &ept0, 0, m, sizeof(simple_msg_t));
        assert(ret == 0);

        esp_amp_platform_delay_ms(500);
    }

    printf("Bye!!\r\n");
    return 0;
}
