/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>

#include "esp_amp.h"
#include "esp_amp_platform.h"

static int lcg_state = 1;

void seed(int s)
{
    if (s == 0) {
        s = 1;
    }
    lcg_state = s;
}

/* NOTE: rand() on subcore will stuck if maincore is in light sleep */
int myrand(void)
{
    lcg_state = (int)((16807ULL * lcg_state) % 2147483647ULL);
    return (int)lcg_state;
}

static int vq_send_isr(void *args)
{
    esp_amp_sw_intr_trigger(SW_INTR_ID_1);
    return 0;
}

int main(void)
{
    printf("Hello!!\r\n");

    /* init esp amp component */
    assert(esp_amp_init() == 0);

    /* seed random number generator */
    seed(esp_amp_platform_get_time_ms());

    /* init queues */
    esp_amp_queue_t vq_mc_master;
    assert(esp_amp_queue_sub_init(&vq_mc_master, NULL, NULL, false, 0) == 0);
    esp_amp_queue_intr_enable(&vq_mc_master, SW_INTR_ID_0);

    esp_amp_queue_t vq_sc_master;
    assert(esp_amp_queue_sub_init(&vq_sc_master, vq_send_isr, NULL, true, 1) == 0);

    /* wait for maincore to configure stuff */
    esp_amp_platform_delay_ms(1500);

    /* main test loop */
    int ret = 0;
    int *buf = NULL;
    uint16_t buf_size = 0;
    for (int i = 0; i < 8; i++) {
        /* subcore sends 2 numbers to maincore, maincore sends their sum back to subcore */
        int a = myrand() % 1000;
        esp_amp_platform_delay_ms(500); /* give maincore some time to go into light sleep */
        ret = esp_amp_queue_alloc_try(&vq_sc_master, (void **)(&buf), sizeof(int));
        assert(ret == 0);
        *buf = a;
        ret = esp_amp_queue_send_try(&vq_sc_master, (void *)(buf), sizeof(int));
        assert(ret == 0);
        printf("subcore sent %d to maincore\n", a);

        int b = myrand() % 1000;
        esp_amp_platform_delay_ms(500); /* give maincore some time to go into light sleep */
        ret = esp_amp_queue_alloc_try(&vq_sc_master, (void **)(&buf), sizeof(int));
        assert(ret == 0);
        *buf = b;
        ret = esp_amp_queue_send_try(&vq_sc_master, (void *)(buf), sizeof(int));
        assert(ret == 0);
        printf("subcore sent %d to maincore\n", b);

        while (esp_amp_queue_recv_try(&vq_mc_master, (void **)(&buf), &buf_size) != 0) {
            esp_amp_platform_delay_us(50);
        }
        assert(buf_size == sizeof(int));
        printf("subcore received %d from maincore\n", *buf);
        assert(*buf == a + b);
        ret = esp_amp_queue_free_try(&vq_mc_master, (void *)(buf));
        assert(ret == 0);

        /* maincore sends 2 numbers to subcore, subcore sends their sum back to maincore */
        int c = 0;
        while (esp_amp_queue_recv_try(&vq_mc_master, (void **)(&buf), &buf_size) != 0) {
            esp_amp_platform_delay_us(50);
        }
        assert(buf_size == sizeof(int));
        printf("subcore received %d from maincore\n", *buf);
        c = *buf;
        ret = esp_amp_queue_free_try(&vq_mc_master, (void *)(buf));
        assert(ret == 0);

        int d = 0;
        while (esp_amp_queue_recv_try(&vq_mc_master, (void **)(&buf), &buf_size) != 0) {
            esp_amp_platform_delay_us(50);
        }
        assert(buf_size == sizeof(int));
        printf("subcore received %d from maincore\n", *buf);
        d = *buf;
        ret = esp_amp_queue_free_try(&vq_mc_master, (void *)(buf));
        assert(ret == 0);

        esp_amp_platform_delay_ms(500); /* give maincore some time to go into light sleep */
        ret = esp_amp_queue_alloc_try(&vq_sc_master, (void **)(&buf), sizeof(int));
        assert(ret == 0);
        *buf = c + d;
        ret = esp_amp_queue_send_try(&vq_sc_master, (void *)(buf), sizeof(int));
        assert(ret == 0);
        printf("subcore sent %d to maincore\n", c + d);
    }

    printf("Bye!!\r\n");
    return 0;
}
