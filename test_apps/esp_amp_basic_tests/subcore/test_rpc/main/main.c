/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>  /* for memcpy, memset */
#include "esp_amp.h"
#include "esp_amp_platform.h"

#define EVENT_SUBCORE_READY (1 << 0)
#define RPC_DEMO_SERVER 0x0001
#define RPC_SRV_NUM 8

/* Command IDs matching maincore test */
#define RPC_CMD_ID_DEMO_1    0x0000
#define RPC_CMD_ID_ECHO      0x0001
#define RPC_CMD_ID_LARGE_RESP 0x0002
#define RPC_CMD_ID_SLOW      0x0003
#define RPC_CMD_ID_BUFFER_TEST 0x0004

static esp_amp_rpmsg_dev_t rpmsg_dev;
static esp_amp_rpc_server_stg_t rpc_server_stg;

static uint8_t req_buf[128];
static uint8_t resp_buf[128];
static uint8_t srv_tbl_stg[sizeof(esp_amp_rpc_service_t) * RPC_SRV_NUM];

/* Service handler implementations */

static void echo_handler(esp_amp_rpc_cmd_t *cmd)
{
    /* Echo back the request data */
    if (cmd->req_len > 0 && cmd->req_data != NULL) {
        uint16_t copy_len = cmd->req_len > cmd->resp_len ? cmd->resp_len : cmd->req_len;
        memcpy(cmd->resp_data, cmd->req_data, copy_len);
        cmd->resp_len = copy_len;
    } else {
        cmd->resp_len = 0;
    }
    cmd->status = ESP_AMP_RPC_STATUS_OK;
}

static void large_resp_handler(esp_amp_rpc_cmd_t *cmd)
{
    /* Send 64-byte response filled with 0xAA pattern */
    uint16_t resp_size = 64;
    if (resp_size > cmd->resp_len) {
        resp_size = cmd->resp_len;
    }

    memset(cmd->resp_data, 0xAA, resp_size);
    cmd->resp_len = resp_size;
    cmd->status = ESP_AMP_RPC_STATUS_OK;
}

static void slow_handler(esp_amp_rpc_cmd_t *cmd)
{
    /* Simulate slow processing - delay 2 seconds */
    esp_amp_platform_delay_us(2000000);  // 2 seconds

    /* Send simple response */
    if (cmd->resp_len > 0) {
        cmd->resp_data[0] = 0x42;
        cmd->resp_len = 1;
    } else {
        cmd->resp_len = 0;
    }
    cmd->status = ESP_AMP_RPC_STATUS_OK;
}

static void buffer_test_handler(esp_amp_rpc_cmd_t *cmd)
{
    /* Invert request data and send back */
    uint16_t copy_len = cmd->req_len > cmd->resp_len ? cmd->resp_len : cmd->req_len;

    for (int i = 0; i < copy_len; i++) {
        cmd->resp_data[i] = ~cmd->req_data[i];
    }

    cmd->resp_len = copy_len;
    cmd->status = ESP_AMP_RPC_STATUS_OK;
}

int main(void)
{
    printf("SUB: Hello!!\r\n");

    assert(esp_amp_init() == 0);
    assert(esp_amp_rpmsg_sub_init(&rpmsg_dev, true, true) == 0);

    /* notify link up with main core */
    esp_amp_event_notify(EVENT_SUBCORE_READY);

    esp_amp_rpc_server_cfg_t cfg = {
        .rpmsg_dev = &rpmsg_dev,
        .server_id = RPC_DEMO_SERVER,
        .stg = &rpc_server_stg,
        .req_buf_len = sizeof(req_buf),
        .resp_buf_len = sizeof(resp_buf),
        .req_buf = req_buf,
        .resp_buf = resp_buf,
        .srv_tbl_len = RPC_SRV_NUM,
        .srv_tbl_stg = srv_tbl_stg,
    };
    esp_amp_rpc_server_t server = esp_amp_rpc_server_init(&cfg);
    assert(server != NULL);
    printf("SUB: rpc server init successfully\r\n");

    /* Register service handlers */
    assert(esp_amp_rpc_server_add_service(server, RPC_CMD_ID_ECHO, echo_handler) == ESP_AMP_RPC_OK);
    assert(esp_amp_rpc_server_add_service(server, RPC_CMD_ID_LARGE_RESP, large_resp_handler) == ESP_AMP_RPC_OK);
    assert(esp_amp_rpc_server_add_service(server, RPC_CMD_ID_SLOW, slow_handler) == ESP_AMP_RPC_OK);
    assert(esp_amp_rpc_server_add_service(server, RPC_CMD_ID_BUFFER_TEST, buffer_test_handler) == ESP_AMP_RPC_OK);
    printf("SUB: rpc services registered successfully\r\n");

    while (true) {
        while (esp_amp_rpmsg_poll(&rpmsg_dev) == 0);
        esp_amp_platform_delay_us(1000);
    }

    printf("SUB: Bye!!\r\n");
    abort();
}
