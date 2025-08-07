/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_amp.h"
#include "esp_amp_env.h"
#include "esp_amp_platform.h"

static uint8_t pending = 0;

static int vq_send_isr(void *args)
{
    esp_amp_sw_intr_trigger(SW_INTR_ID_1);
    return 0;
}

static int vq_recv_isr(void *args)
{
    esp_amp_env_enter_critical();
    pending++;
    esp_amp_env_exit_critical();
    return 0;
}

static void wait()
{
    while (true) {
        esp_amp_env_enter_critical();
        if (pending == 0) {
            esp_amp_env_exit_critical();
            esp_amp_platform_delay_us(50);
            continue;
        }
        pending--;
        esp_amp_env_exit_critical();
        break;
    }
}

int main(void)
{
    printf("SUB: Hello!!\r\n");
    assert(esp_amp_init() == 0);

    esp_amp_queue_t vq_mc_master;
    assert(esp_amp_queue_sub_init(&vq_mc_master, vq_recv_isr, NULL, false, 0) == 0);
    esp_amp_queue_intr_enable(&vq_mc_master, SW_INTR_ID_0);

    esp_amp_queue_t vq_sc_master;
    assert(esp_amp_queue_sub_init(&vq_sc_master, vq_send_isr, NULL, true, 1) == 0);

    int ret = 0;
    int *buf = NULL;
    uint16_t buf_size = 0;

    for (int i = 0; i < 8; i++) {
        /* subcore sends 2 numbers to maincore, maincore sends their sum back to subcore */
        int a = rand() % 999;
        ret = esp_amp_queue_alloc_try(&vq_sc_master, (void **)(&buf), sizeof(int));
        assert(ret == 0);
        *buf = a;
        ret = esp_amp_queue_send_try(&vq_sc_master, (void *)(buf), sizeof(int));
        assert(ret == 0);
        printf("subcore sent %d to maincore\n", a);

        int b = rand() % 999;
        ret = esp_amp_queue_alloc_try(&vq_sc_master, (void **)(&buf), sizeof(int));
        assert(ret == 0);
        *buf = b;
        ret = esp_amp_queue_send_try(&vq_sc_master, (void *)(buf), sizeof(int));
        assert(ret == 0);
        printf("subcore sent %d to maincore\n", b);

        wait();
        ret = esp_amp_queue_recv_try(&vq_mc_master, (void **)(&buf), &buf_size);
        assert(ret == 0);
        assert(buf_size == sizeof(int));
        printf("subcore received %d from maincore\n", *buf);
        assert(*buf == a + b);
        ret = esp_amp_queue_free_try(&vq_mc_master, (void *)(buf));
        assert(ret == 0);

        /* maincore sends 2 numbers to subcore, subcore sends their sum back to maincore */
        wait();
        int c = 0;
        ret = esp_amp_queue_recv_try(&vq_mc_master, (void **)(&buf), &buf_size);
        assert(ret == 0);
        assert(buf_size == sizeof(int));
        printf("subcore received %d from maincore\n", *buf);
        c = *buf;
        ret = esp_amp_queue_free_try(&vq_mc_master, (void *)(buf));
        assert(ret == 0);

        wait();
        int d = 0;
        ret = esp_amp_queue_recv_try(&vq_mc_master, (void **)(&buf), &buf_size);
        assert(ret == 0);
        assert(buf_size == sizeof(int));
        printf("subcore received %d from maincore\n", *buf);
        d = *buf;
        ret = esp_amp_queue_free_try(&vq_mc_master, (void *)(buf));
        assert(ret == 0);

        ret = esp_amp_queue_alloc_try(&vq_sc_master, (void **)(&buf), sizeof(int));
        assert(ret == 0);
        *buf = c + d;
        ret = esp_amp_queue_send_try(&vq_sc_master, (void *)(buf), sizeof(int));
        assert(ret == 0);
        printf("subcore sent %d to maincore\n", c + d);
    }

    printf("SUB: Bye!!\r\n");
    return 0;
}
