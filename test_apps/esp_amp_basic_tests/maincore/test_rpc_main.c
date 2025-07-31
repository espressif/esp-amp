/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_amp.h"
#include "esp_err.h"

#include "unity.h"
#include "unity_test_runner.h"

#define RPC_MAIN_CORE_CLIENT 0x0000
#define RPC_MAIN_CORE_SERVER 0x0001
#define RPC_CMD_ID_DEMO_1    0x0000
#define RPC_CMD_ID_ECHO      0x0001
#define RPC_CMD_ID_LARGE_RESP 0x0002
#define RPC_CMD_ID_SLOW      0x0003
#define RPC_CMD_ID_BUFFER_TEST 0x0004

TEST_CASE("RPC client init/deinit", "[esp_amp]")
{
    esp_amp_rpmsg_dev_t* rpmsg_dev = (esp_amp_rpmsg_dev_t*)(malloc(sizeof(esp_amp_rpmsg_dev_t)));
    TEST_ASSERT_NOT_NULL(rpmsg_dev);

    TEST_ASSERT(esp_amp_init() == 0);
    TEST_ASSERT(esp_amp_rpmsg_main_init(rpmsg_dev, 32, 64, false, false) == 0);

    esp_amp_rpc_client_stg_t rpc_client_stg;
    esp_amp_rpc_client_cfg_t cfg = {
        .client_id = RPC_MAIN_CORE_CLIENT,
        .server_id = RPC_MAIN_CORE_SERVER,
        .rpmsg_dev = rpmsg_dev,
        .stg = NULL,
        .poll_cb = NULL,
        .poll_arg = NULL,
    };

    /* fail if stg is NULL */
    TEST_ASSERT_EQUAL(NULL, esp_amp_rpc_client_init(&cfg));

    /* success if params are valid */
    cfg.stg = &rpc_client_stg;
    esp_amp_rpc_client_t client = esp_amp_rpc_client_init(&cfg);
    TEST_ASSERT_NOT_EQUAL(NULL, client);
    esp_amp_rpc_client_deinit(client);

    /* fail if rpmsg_dev is NULL */
    cfg.rpmsg_dev = NULL;
    TEST_ASSERT_EQUAL(NULL, esp_amp_rpc_client_init(&cfg));

    free(rpmsg_dev);
    vTaskDelay(pdMS_TO_TICKS(500));
}

TEST_CASE("RPC server init/deinit", "[esp_amp]")
{
    esp_amp_rpmsg_dev_t* rpmsg_dev = (esp_amp_rpmsg_dev_t*)(malloc(sizeof(esp_amp_rpmsg_dev_t)));
    TEST_ASSERT_NOT_NULL(rpmsg_dev);

    TEST_ASSERT(esp_amp_init() == 0);
    TEST_ASSERT(esp_amp_rpmsg_main_init(rpmsg_dev, 32, 64, false, false) == 0);

    esp_amp_rpc_server_stg_t rpc_server_stg;
    uint8_t req_buf[64];
    uint8_t resp_buf[64];
    uint8_t srv_tbl_stg[sizeof(esp_amp_rpc_service_t) * 2];
    esp_amp_rpc_server_cfg_t cfg = {
        .rpmsg_dev = rpmsg_dev,
        .server_id = RPC_MAIN_CORE_SERVER,
        .stg = NULL,
        .req_buf_len = sizeof(req_buf),
        .resp_buf_len = sizeof(resp_buf),
        .req_buf = req_buf,
        .resp_buf = resp_buf,
        .srv_tbl_len = 2,
        .srv_tbl_stg = srv_tbl_stg,
    };

    /* fail if stg is NULL */
    TEST_ASSERT_EQUAL(NULL, esp_amp_rpc_server_init(&cfg));

    /* success if params are valid */
    cfg.stg = &rpc_server_stg;
    esp_amp_rpc_server_t server = esp_amp_rpc_server_init(&cfg);
    TEST_ASSERT_NOT_EQUAL(NULL, server);
    esp_amp_rpc_server_deinit(server);

    /* fail if rpmsg_dev is NULL */
    cfg.rpmsg_dev = NULL;
    TEST_ASSERT_EQUAL(NULL, esp_amp_rpc_server_init(&cfg));

    /* fail if req_buf is NULL */
    cfg.rpmsg_dev = rpmsg_dev;
    cfg.req_buf = NULL;
    TEST_ASSERT_EQUAL(NULL, esp_amp_rpc_server_init(&cfg));

    /* fail if resp_buf is NULL */
    cfg.req_buf = req_buf;
    cfg.resp_buf = NULL;
    TEST_ASSERT_EQUAL(NULL, esp_amp_rpc_server_init(&cfg));

    /* fail if srv_tbl_stg is NULL */
    cfg.resp_buf = resp_buf;
    cfg.srv_tbl_stg = NULL;
    TEST_ASSERT_EQUAL(NULL, esp_amp_rpc_server_init(&cfg));

    /* fail if srv_tbl_len is 0 */
    cfg.srv_tbl_stg = srv_tbl_stg;
    cfg.srv_tbl_len = 0;
    TEST_ASSERT_EQUAL(NULL, esp_amp_rpc_server_init(&cfg));

    free(rpmsg_dev);
    vTaskDelay(pdMS_TO_TICKS(500));
}

static void rpc_cmd_handler_test(esp_amp_rpc_cmd_t *cmd)
{
    return;
}

TEST_CASE("RPC server add/del service", "[esp_amp]")
{
    esp_amp_rpmsg_dev_t* rpmsg_dev = (esp_amp_rpmsg_dev_t*)(malloc(sizeof(esp_amp_rpmsg_dev_t)));
    TEST_ASSERT_NOT_NULL(rpmsg_dev);

    TEST_ASSERT(esp_amp_init() == 0);
    TEST_ASSERT(esp_amp_rpmsg_main_init(rpmsg_dev, 32, 64, false, false) == 0);

    /* init server */
    esp_amp_rpc_server_stg_t rpc_server_stg;
    uint8_t req_buf[64];
    uint8_t resp_buf[64];
    uint8_t srv_tbl_stg[sizeof(esp_amp_rpc_service_t) * 2];
    esp_amp_rpc_server_cfg_t cfg = {
        .rpmsg_dev = rpmsg_dev,
        .server_id = RPC_MAIN_CORE_SERVER,
        .stg = &rpc_server_stg,
        .req_buf_len = sizeof(req_buf),
        .resp_buf_len = sizeof(resp_buf),
        .req_buf = req_buf,
        .resp_buf = resp_buf,
        .srv_tbl_len = 2,
        .srv_tbl_stg = srv_tbl_stg,
    };

    esp_amp_rpc_server_t server = esp_amp_rpc_server_init(&cfg);
    TEST_ASSERT_NOT_EQUAL(NULL, server);

    /* handler added to same cmd id will report error */
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_OK, esp_amp_rpc_server_add_service(server, RPC_CMD_ID_DEMO_1, rpc_cmd_handler_test));
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_ERR_EXIST, esp_amp_rpc_server_add_service(server, RPC_CMD_ID_DEMO_1, rpc_cmd_handler_test));

    /* handler delete a valid cmd id will succeed */
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_OK, esp_amp_rpc_server_del_service(server, RPC_CMD_ID_DEMO_1));

    /* handler delete an invalid cmd id will report error */
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_ERR_NOT_FOUND, esp_amp_rpc_server_del_service(server, RPC_CMD_ID_DEMO_1));

    esp_amp_rpc_server_deinit(server);

    free(rpmsg_dev);
    vTaskDelay(pdMS_TO_TICKS(500));
}

TEST_CASE("RPC client exec cmd", "[esp_amp]")
{
    esp_amp_rpmsg_dev_t* rpmsg_dev = (esp_amp_rpmsg_dev_t*)(malloc(sizeof(esp_amp_rpmsg_dev_t)));
    TEST_ASSERT_NOT_NULL(rpmsg_dev);

    TEST_ASSERT(esp_amp_init() == 0);
    TEST_ASSERT(esp_amp_rpmsg_main_init(rpmsg_dev, 32, 64, false, false) == 0);

    esp_amp_rpc_client_stg_t rpc_client_stg;
    esp_amp_rpc_client_cfg_t cfg = {
        .client_id = RPC_MAIN_CORE_CLIENT,
        .server_id = RPC_MAIN_CORE_SERVER,
        .rpmsg_dev = rpmsg_dev,
        .stg = &rpc_client_stg,
        .poll_cb = NULL,
        .poll_arg = NULL,
    };

    esp_amp_rpc_client_t client = esp_amp_rpc_client_init(&cfg);
    TEST_ASSERT_NOT_EQUAL(NULL, client);

    /* exec a valid cmd id will succeed */
    esp_amp_rpc_cmd_t cmd = { 0 };
    cmd.cmd_id = RPC_CMD_ID_DEMO_1;
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_OK, esp_amp_rpc_client_execute_cmd(client, &cmd));

    /* exec an invalid cmd id will report error */
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_ERR_INVALID_ARG, esp_amp_rpc_client_execute_cmd(client, NULL));
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_ERR_INVALID_ARG, esp_amp_rpc_client_execute_cmd(NULL, &cmd));

    esp_amp_rpc_client_deinit(client);

    /* exec after deinit will report error */
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_ERR_INVALID_STATE, esp_amp_rpc_client_execute_cmd(client, &cmd));

    free(rpmsg_dev);
    vTaskDelay(pdMS_TO_TICKS(500));
}

#define EVENT_SUBCORE_READY (1 << 0)

static void cmd_demo_cb(esp_amp_rpc_client_t client, esp_amp_rpc_cmd_t *cmd, void *arg)
{
    TaskHandle_t task = (TaskHandle_t)arg;
    BaseType_t need_yield = false;

    vTaskNotifyGiveFromISR(task, &need_yield);
    portYIELD_FROM_ISR(need_yield);
}

extern const uint8_t subcore_rpc_test_bin_start[] asm("_binary_subcore_test_rpc_bin_start");
extern const uint8_t subcore_rpc_test_bin_end[]   asm("_binary_subcore_test_rpc_bin_end");

TEST_CASE("RPC invalid cmd test", "[esp_amp]")
{
    esp_amp_rpc_client_stg_t rpc_client_stg;
    esp_amp_rpmsg_dev_t rpmsg_dev;

    /* init esp amp */
    assert(esp_amp_init() == 0);
    assert(esp_amp_rpmsg_main_init(&rpmsg_dev, 8, 128, false, false) == 0);
    esp_amp_rpmsg_intr_enable(&rpmsg_dev);

    /* Load firmware & start subcore */
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_rpc_test_bin_start));
    ESP_ERROR_CHECK(esp_amp_start_subcore());

    /* wait for link up */
    assert((esp_amp_event_wait(EVENT_SUBCORE_READY, true, true, 10000) & EVENT_SUBCORE_READY) == EVENT_SUBCORE_READY);

    /* init client */
    esp_amp_rpc_client_cfg_t cfg = {
        .client_id = RPC_MAIN_CORE_CLIENT,
        .server_id = RPC_MAIN_CORE_SERVER,
        .rpmsg_dev = &rpmsg_dev,
        .stg = &rpc_client_stg,
        .poll_cb = NULL,
        .poll_arg = NULL,
    };
    esp_amp_rpc_client_t client = esp_amp_rpc_client_init(&cfg);
    TEST_ASSERT_NOT_EQUAL(NULL, client);

    /* exec an invalid cmd id expect ESP_AMP_RPC_STATUS_INVALID_CMD */
    esp_amp_rpc_cmd_t cmd = { 0 };
    cmd.cmd_id = RPC_CMD_ID_DEMO_1;
    cmd.cb = cmd_demo_cb;
    cmd.cb_arg = (void*)xTaskGetCurrentTaskHandle();
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_OK, esp_amp_rpc_client_execute_cmd(client, &cmd));
    ulTaskNotifyTake(true, pdMS_TO_TICKS(1000)); /* wait up to 1000ms */
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_STATUS_INVALID_CMD, cmd.status);

    esp_amp_rpc_client_deinit(client);
}

TEST_CASE("RPC response buffer truncation", "[esp_amp]")
{
    esp_amp_rpc_client_stg_t rpc_client_stg;
    esp_amp_rpmsg_dev_t rpmsg_dev;

    /* init esp amp */
    assert(esp_amp_init() == 0);
    assert(esp_amp_rpmsg_main_init(&rpmsg_dev, 8, 128, false, false) == 0);
    esp_amp_rpmsg_intr_enable(&rpmsg_dev);

    /* Load firmware & start subcore */
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_rpc_test_bin_start));
    ESP_ERROR_CHECK(esp_amp_start_subcore());

    /* wait for link up */
    assert((esp_amp_event_wait(EVENT_SUBCORE_READY, true, true, 10000) & EVENT_SUBCORE_READY) == EVENT_SUBCORE_READY);

    /* init client */
    esp_amp_rpc_client_cfg_t cfg = {
        .client_id = RPC_MAIN_CORE_CLIENT,
        .server_id = RPC_MAIN_CORE_SERVER,
        .rpmsg_dev = &rpmsg_dev,
        .stg = &rpc_client_stg,
        .poll_cb = NULL,
        .poll_arg = NULL,
    };
    esp_amp_rpc_client_t client = esp_amp_rpc_client_init(&cfg);
    TEST_ASSERT_NOT_EQUAL(NULL, client);

    /* Test: Small response buffer should not overflow when server sends large response */
    uint8_t resp_buf[64] = { 0 };  // resp_buf is 64 bytes, but only 16 bytes are requested
    uint8_t resp_len = 16;
    esp_amp_rpc_cmd_t cmd = { 0 };
    cmd.cmd_id = RPC_CMD_ID_LARGE_RESP;  // Server will send 64-byte response
    cmd.resp_data = resp_buf;
    cmd.resp_len = resp_len;  // Only 16 bytes are requested
    cmd.cb = cmd_demo_cb;
    cmd.cb_arg = (void*)xTaskGetCurrentTaskHandle();

    TEST_ASSERT_EQUAL(ESP_AMP_RPC_OK, esp_amp_rpc_client_execute_cmd(client, &cmd));
    ulTaskNotifyTake(true, pdMS_TO_TICKS(1000));

    /* Should succeed with truncated response */
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_STATUS_OK, cmd.status);
    TEST_ASSERT_EQUAL(resp_len, cmd.resp_len);  // Should be truncated to requested size

    /* Verify buffer integrity - check pattern in response */
    for (int i = 0; i < resp_len; i++) {
        TEST_ASSERT_EQUAL(0xAA, resp_buf[i]);  // Server fills with 0xAA pattern
    }

    /* Verify buffer beyond buffer size is not modified*/
    for (int i = resp_len; i < sizeof(resp_buf); i++) {
        TEST_ASSERT_EQUAL(0, resp_buf[i]);
    }

    esp_amp_rpc_client_deinit(client);
}

TEST_CASE("RPC request length overflow detection", "[esp_amp]")
{
    esp_amp_rpmsg_dev_t* rpmsg_dev = (esp_amp_rpmsg_dev_t*)(malloc(sizeof(esp_amp_rpmsg_dev_t)));
    TEST_ASSERT_NOT_NULL(rpmsg_dev);

    TEST_ASSERT(esp_amp_init() == 0);
    TEST_ASSERT(esp_amp_rpmsg_main_init(rpmsg_dev, 32, 128, false, false) == 0);

    esp_amp_rpc_client_stg_t rpc_client_stg;
    esp_amp_rpc_client_cfg_t cfg = {
        .client_id = RPC_MAIN_CORE_CLIENT,
        .server_id = RPC_MAIN_CORE_SERVER,
        .rpmsg_dev = rpmsg_dev,
        .stg = &rpc_client_stg,
        .poll_cb = NULL,
        .poll_arg = NULL,
    };

    esp_amp_rpc_client_t client = esp_amp_rpc_client_init(&cfg);
    TEST_ASSERT_NOT_EQUAL(NULL, client);

    /* Test: Request length that would cause overflow */
    esp_amp_rpc_cmd_t cmd = { 0 };
    cmd.cmd_id = RPC_CMD_ID_ECHO;
    cmd.req_len = UINT16_MAX - 4;  // Would overflow when adding header size
    cmd.req_data = NULL;  // Don't need actual data for this test

    /* Should return invalid size error */
    TEST_ASSERT_EQUAL(ESP_AMP_RPC_ERR_INVALID_SIZE, esp_amp_rpc_client_execute_cmd(client, &cmd));

    esp_amp_rpc_client_deinit(client);
}

TEST_CASE("RPC message ID wraparound", "[esp_amp]")
{
    esp_amp_rpc_client_stg_t rpc_client_stg;
    esp_amp_rpmsg_dev_t rpmsg_dev;

    /* init esp amp */
    assert(esp_amp_init() == 0);
    assert(esp_amp_rpmsg_main_init(&rpmsg_dev, 8, 128, false, false) == 0);
    esp_amp_rpmsg_intr_enable(&rpmsg_dev);

    /* Load firmware & start subcore */
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_rpc_test_bin_start));
    ESP_ERROR_CHECK(esp_amp_start_subcore());

    /* wait for link up */
    assert((esp_amp_event_wait(EVENT_SUBCORE_READY, true, true, 10000) & EVENT_SUBCORE_READY) == EVENT_SUBCORE_READY);

    /* init client */
    esp_amp_rpc_client_cfg_t cfg = {
        .client_id = RPC_MAIN_CORE_CLIENT,
        .server_id = RPC_MAIN_CORE_SERVER,
        .rpmsg_dev = &rpmsg_dev,
        .stg = &rpc_client_stg,
        .poll_cb = NULL,
        .poll_arg = NULL,
    };
    esp_amp_rpc_client_t client = esp_amp_rpc_client_init(&cfg);
    TEST_ASSERT_NOT_EQUAL(NULL, client);

    /* Force message ID to near overflow */
    esp_amp_rpc_client_inst_t *client_inst = (esp_amp_rpc_client_inst_t *)client;
    client_inst->pending_id = UINT16_MAX - 2;

    /* Test multiple commands across wraparound */
    for (int i = 0; i < 5; i++) {
        int req_data = i;
        int resp_data = -1;
        esp_amp_rpc_cmd_t cmd = { 0 };
        cmd.cmd_id = RPC_CMD_ID_ECHO;
        cmd.req_data = (uint8_t*)&req_data; /* send integer */
        cmd.req_len = sizeof(req_data);
        cmd.resp_data = (uint8_t*)&resp_data;
        cmd.resp_len = sizeof(resp_data);
        cmd.cb = cmd_demo_cb;
        cmd.cb_arg = (void*)xTaskGetCurrentTaskHandle();

        TEST_ASSERT_EQUAL(ESP_AMP_RPC_OK, esp_amp_rpc_client_execute_cmd(client, &cmd));
        ulTaskNotifyTake(true, pdMS_TO_TICKS(1000));
        TEST_ASSERT_EQUAL(ESP_AMP_RPC_STATUS_OK, cmd.status);
        /* verify response data matches request data when wrapped around */
        TEST_ASSERT_EQUAL(req_data, resp_data);
    }

    esp_amp_rpc_client_deinit(client);
}

TEST_CASE("RPC packet full size test", "[esp_amp]")
{
    esp_amp_rpc_client_stg_t rpc_client_stg;
    esp_amp_rpmsg_dev_t rpmsg_dev;

    /* init esp amp with larger buffers */
    assert(esp_amp_init() == 0);
    assert(esp_amp_rpmsg_main_init(&rpmsg_dev, 8, 128, false, false) == 0);
    esp_amp_rpmsg_intr_enable(&rpmsg_dev);

    /* Load firmware & start subcore */
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_rpc_test_bin_start));
    ESP_ERROR_CHECK(esp_amp_start_subcore());

    /* wait for link up */
    assert((esp_amp_event_wait(EVENT_SUBCORE_READY, true, true, 10000) & EVENT_SUBCORE_READY) == EVENT_SUBCORE_READY);

    /* init client */
    esp_amp_rpc_client_cfg_t cfg = {
        .client_id = RPC_MAIN_CORE_CLIENT,
        .server_id = RPC_MAIN_CORE_SERVER,
        .rpmsg_dev = &rpmsg_dev,
        .stg = &rpc_client_stg,
        .poll_cb = NULL,
        .poll_arg = NULL,
    };
    esp_amp_rpc_client_t client = esp_amp_rpc_client_init(&cfg);
    TEST_ASSERT_NOT_EQUAL(NULL, client);

    /* Test large request and response */
    uint8_t large_req_data[128] = { 0 };
    uint8_t large_resp_data[128] = { 0 };

    /* test rpc packet full size*/
    size_t rpc_pkt_len = esp_amp_rpmsg_get_max_size(&rpmsg_dev) - sizeof(esp_amp_rpc_pkt_t);
    printf("rpc_pkt_len: %d\n", rpc_pkt_len);

    /* Fill request with pattern */
    memset(large_req_data, 0xAA, rpc_pkt_len);

    esp_amp_rpc_cmd_t cmd = { 0 };
    cmd.cmd_id = RPC_CMD_ID_BUFFER_TEST;
    cmd.req_data = large_req_data;
    cmd.req_len = rpc_pkt_len;
    cmd.resp_data = large_resp_data;
    cmd.resp_len = rpc_pkt_len;
    cmd.cb = cmd_demo_cb;
    cmd.cb_arg = (void*)xTaskGetCurrentTaskHandle();

    TEST_ASSERT_EQUAL(ESP_AMP_RPC_OK, esp_amp_rpc_client_execute_cmd(client, &cmd));
    ulTaskNotifyTake(true, pdMS_TO_TICKS(1000));

    TEST_ASSERT_EQUAL(ESP_AMP_RPC_STATUS_OK, cmd.status);
    TEST_ASSERT_TRUE(cmd.resp_len > 0);

    /* Verify response data integrity */
    for (int i = 0; i < rpc_pkt_len; i++) {
        TEST_ASSERT_EQUAL((uint8_t)(~large_req_data[i]), large_resp_data[i]);  // Server inverts the data
    }

    esp_amp_rpc_client_deinit(client);
}

TEST_CASE("RPC stress test and rapid commands", "[esp_amp]")
{
    esp_amp_rpc_client_stg_t rpc_client_stg;
    esp_amp_rpmsg_dev_t rpmsg_dev;

    /* init esp amp */
    assert(esp_amp_init() == 0);
    assert(esp_amp_rpmsg_main_init(&rpmsg_dev, 16, 128, false, false) == 0);
    esp_amp_rpmsg_intr_enable(&rpmsg_dev);

    /* Load firmware & start subcore */
    TEST_ASSERT_EQUAL(ESP_OK, esp_amp_load_sub(subcore_rpc_test_bin_start));
    ESP_ERROR_CHECK(esp_amp_start_subcore());

    /* wait for link up */
    assert((esp_amp_event_wait(EVENT_SUBCORE_READY, true, true, 10000) & EVENT_SUBCORE_READY) == EVENT_SUBCORE_READY);

    /* init client */
    esp_amp_rpc_client_cfg_t cfg = {
        .client_id = RPC_MAIN_CORE_CLIENT,
        .server_id = RPC_MAIN_CORE_SERVER,
        .rpmsg_dev = &rpmsg_dev,
        .stg = &rpc_client_stg,
        .poll_cb = NULL,
        .poll_arg = NULL,
    };
    esp_amp_rpc_client_t client = esp_amp_rpc_client_init(&cfg);
    TEST_ASSERT_NOT_EQUAL(NULL, client);

    /* Stress test: Send many rapid commands */
    int success_count = 0;
    const int total_commands = 1000;

    for (int i = 0; i < total_commands; i++) {
        esp_amp_rpc_cmd_t cmd = { 0 };
        cmd.cmd_id = RPC_CMD_ID_ECHO;

        /* Create unique request data for each command */
        int req_data = i;
        int resp_data = -1;
        cmd.req_data = (uint8_t*)&req_data;
        cmd.req_len = sizeof(req_data);
        cmd.resp_data = (uint8_t*)&resp_data;
        cmd.resp_len = sizeof(resp_data);
        cmd.cb = cmd_demo_cb;
        cmd.cb_arg = (void*)xTaskGetCurrentTaskHandle();

        int result = esp_amp_rpc_client_execute_cmd(client, &cmd);
        if (result == ESP_AMP_RPC_OK) {
            ulTaskNotifyTake(true, pdMS_TO_TICKS(100));  // Short timeout for rapid testing
            if (cmd.status == ESP_AMP_RPC_STATUS_OK && resp_data == req_data) {
                success_count++;
            }
        }

        /* Small delay to prevent overwhelming */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* Should have high success rate (allow some failures due to rapid sending) */
    TEST_ASSERT_GREATER_THAN(total_commands * 0.8, success_count);  // At least 80% success
    printf("Stress test: %d/%d commands succeeded\n", success_count, total_commands);

    esp_amp_rpc_client_deinit(client);
}
