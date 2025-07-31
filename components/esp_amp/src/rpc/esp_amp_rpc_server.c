/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "string.h"
#include "esp_attr.h"
#include "esp_amp_log.h"
#include "esp_amp_env.h"
#include "esp_amp_rpmsg.h"
#include "esp_amp_rpc.h"

static const DRAM_ATTR char __attribute__((unused)) TAG[] = "esp_amp_rpc_server";

typedef struct {
    uint16_t client_addr;
    uint16_t pkt_len;
    esp_amp_rpc_pkt_t *pkt;
} esp_amp_rpc_pkt_digest_t;

static int server_cb(void* data, uint16_t data_len, uint16_t src_addr, void* priv_data);

esp_amp_rpc_server_t esp_amp_rpc_server_init(esp_amp_rpc_server_cfg_t *cfg)
{
    if (cfg == NULL || cfg->stg == NULL || cfg->rpmsg_dev == NULL) {
        return NULL;
    }

    if (cfg->req_buf_len == 0 || cfg->resp_buf_len == 0 || cfg->req_buf == NULL || cfg->resp_buf == NULL) {
        return NULL;
    }

    if (cfg->srv_tbl_len == 0 || cfg->srv_tbl_stg == NULL) {
        return NULL;
    }

    esp_amp_rpc_server_inst_t *server_inst = (esp_amp_rpc_server_inst_t *)cfg->stg;
    esp_amp_rpmsg_ept_t *rpmsg_ept = esp_amp_rpmsg_create_endpoint(cfg->rpmsg_dev, cfg->server_id, server_cb, server_inst, &server_inst->rpmsg_ept);
    if (rpmsg_ept == NULL) {
        return NULL;
    }

    /* create queue */
    if (cfg->queue_len == 0) {
        cfg->queue_len = 4;
    }

    /* clear storage buffer before use */
    memset(cfg->srv_tbl_stg, 0, sizeof(esp_amp_rpc_service_t) * cfg->srv_tbl_len);

    int ret = esp_amp_env_queue_create(&server_inst->queue, cfg->queue_len, sizeof(esp_amp_rpc_pkt_digest_t));
    if (ret != 0) {
        return NULL;
    }

    esp_amp_env_enter_critical();
    server_inst->req_buf_len = cfg->req_buf_len;
    server_inst->resp_buf_len = cfg->resp_buf_len;
    server_inst->req_buf = cfg->req_buf;
    server_inst->resp_buf = cfg->resp_buf;
    server_inst->server_id = cfg->server_id;
    server_inst->rpmsg_dev = cfg->rpmsg_dev;
    server_inst->srv_tbl_len = cfg->srv_tbl_len;
    server_inst->srv = (esp_amp_rpc_service_t *)cfg->srv_tbl_stg;
    server_inst->running = true;
    esp_amp_env_exit_critical();
    return server_inst;
}

void esp_amp_rpc_server_deinit(esp_amp_rpc_server_t server)
{
    esp_amp_rpc_server_inst_t *server_inst = (esp_amp_rpc_server_inst_t *)server;
    if (server_inst == NULL || server_inst->running == false) {
        return;
    }

    void *queue = server_inst->queue;

    esp_amp_env_enter_critical();
    // First stop accepting new requests
    server_inst->running = false;
    esp_amp_env_exit_critical();

    // Drain pending messages to prevent resource leaks
    esp_amp_rpc_pkt_digest_t req_pkt_digest;
    while (queue && esp_amp_env_queue_recv(queue, &req_pkt_digest, 0) == 0) {
        esp_amp_rpmsg_destroy(server_inst->rpmsg_dev, req_pkt_digest.pkt);
    }

    esp_amp_env_enter_critical();
    esp_amp_rpmsg_delete_endpoint(server_inst->rpmsg_dev, server_inst->server_id);
    memset(server_inst, 0, sizeof(esp_amp_rpc_server_inst_t));
    esp_amp_env_exit_critical();

    esp_amp_env_queue_delete(queue);
}

int esp_amp_rpc_server_add_service(esp_amp_rpc_server_t server, uint16_t cmd_id, esp_amp_rpc_cmd_handler_t handler)
{
    esp_amp_rpc_server_inst_t *server_inst = (esp_amp_rpc_server_inst_t *)server;
    if (server_inst == NULL || server_inst->srv == NULL) {
        return ESP_AMP_RPC_ERR_INVALID_ARG;
    }

    if (handler == NULL) {
        return ESP_AMP_RPC_ERR_INVALID_ARG;
    }

    int ret = ESP_AMP_RPC_ERR_NO_MEM;

    int empty_idx = -1;
    int exist_idx = -1;
    esp_amp_env_enter_critical();
    for (int i = 0; i < server_inst->srv_tbl_len; i++) {
        if (server_inst->srv[i].handler == NULL) {
            if (empty_idx == -1) {
                empty_idx = i;
            }
        } else if (server_inst->srv[i].cmd_id == cmd_id) {
            exist_idx = i;
        }
    }
    if (exist_idx != -1) { /* service already exist */
        ret = ESP_AMP_RPC_ERR_EXIST;
    } else if (empty_idx != -1) { /* service not exist && service table is not full */
        server_inst->srv[empty_idx].cmd_id = cmd_id;
        server_inst->srv[empty_idx].handler = handler;
        ret = ESP_AMP_RPC_OK;
    }
    esp_amp_env_exit_critical();
    return ret;
}

int esp_amp_rpc_server_del_service(esp_amp_rpc_server_t server, uint16_t cmd_id)
{
    esp_amp_rpc_server_inst_t *server_inst = (esp_amp_rpc_server_inst_t *)server;
    if (server_inst == NULL || server_inst->srv == NULL) {
        return ESP_AMP_RPC_ERR_INVALID_ARG;
    }
    int ret = ESP_AMP_RPC_ERR_NOT_FOUND;
    esp_amp_env_enter_critical();
    for (int i = 0; i < server_inst->srv_tbl_len; i++) {
        if (server_inst->srv[i].cmd_id == cmd_id && server_inst->srv[i].handler != NULL) {
            server_inst->srv[i].cmd_id = 0;
            server_inst->srv[i].handler = NULL;
            ret = ESP_AMP_RPC_OK;
        }
    }
    esp_amp_env_exit_critical();
    return ret;
}

static void exec_cmd_and_send(esp_amp_rpc_server_inst_t *server_inst, esp_amp_rpc_pkt_t *req_pkt, uint16_t client_addr)
{
    /* copy request buffer to server buffer */
    uint16_t req_buf_len = (req_pkt->msg_len > server_inst->req_buf_len) ? server_inst->req_buf_len : req_pkt->msg_len;
    uint16_t resp_buf_len = server_inst->resp_buf_len;
    uint16_t cmd_id = req_pkt->cmd_id;
    uint16_t msg_id = req_pkt->msg_id;
    memcpy(server_inst->req_buf, req_pkt->msg_data, req_buf_len);

    /* destroy request */
    esp_amp_rpmsg_destroy(server_inst->rpmsg_dev, req_pkt);

    /* construct param cmd for service handler */
    esp_amp_rpc_cmd_t cmd = {
        .req_data = server_inst->req_buf,
        .req_len = req_buf_len,
        .resp_data = server_inst->resp_buf,
        .resp_len = resp_buf_len,
        .cmd_id = cmd_id,
        .status = ESP_AMP_RPC_STATUS_PENDING,
    };

    /* find service handler */
    esp_amp_rpc_cmd_handler_t handler = NULL;
    esp_amp_env_enter_critical();
    for (int i = 0; i < server_inst->srv_tbl_len; i++) {
        if (server_inst->srv[i].handler != NULL && server_inst->srv[i].cmd_id == cmd_id) {
            handler = server_inst->srv[i].handler;
            break;
        }
    }
    esp_amp_env_exit_critical();

    /* execute handler */
    if (handler == NULL) {
        cmd.status = ESP_AMP_RPC_STATUS_INVALID_CMD; /* even invalid cmd, still need to send response */
        cmd.resp_len = 1; /* non-zero length to invoke sending */
        cmd.resp_data[0] = 0;
    } else {
        handler(&cmd);
    }

    /* only send response if response is needed */
    if (cmd.resp_len > 0) {
        uint16_t resp_pkt_buf_max_len = esp_amp_rpmsg_get_max_size(server_inst->rpmsg_dev);
        uint8_t *resp_pkt_buf = esp_amp_rpmsg_create_message(server_inst->rpmsg_dev, resp_pkt_buf_max_len, ESP_AMP_RPMSG_DATA_DEFAULT);
        if (resp_pkt_buf == NULL) {
            return;
        }

        uint16_t msg_max_len = resp_pkt_buf_max_len - sizeof(esp_amp_rpc_pkt_t);
        uint16_t msg_len = cmd.resp_len > msg_max_len ? msg_max_len : cmd.resp_len;
        esp_amp_rpc_pkt_t resp_pkt = {
            .msg_id = msg_id,
            .cmd_id = cmd_id,
            .status = cmd.status,
            .msg_len = msg_len,
        };
        memcpy(resp_pkt_buf, &resp_pkt, sizeof(esp_amp_rpc_pkt_t));
        memcpy(resp_pkt_buf + sizeof(esp_amp_rpc_pkt_t), cmd.resp_data, msg_len);

        /* send response, msg is cut off to fit in send buffer. will always success */
        esp_amp_rpmsg_send_nocopy(server_inst->rpmsg_dev, &server_inst->rpmsg_ept, client_addr, resp_pkt_buf, sizeof(esp_amp_rpc_pkt_t) + msg_len);
    }
}

#if !IS_ENV_BM
static int IRAM_ATTR server_cb(void* data, uint16_t data_len, uint16_t src_addr, void* priv_data)
{
    esp_amp_rpc_server_inst_t *server_inst = (esp_amp_rpc_server_inst_t *)priv_data;
    esp_amp_rpc_pkt_t *req_pkt = (esp_amp_rpc_pkt_t *)data;
    if (server_inst == NULL || server_inst->running == false || req_pkt == NULL) {
        return ESP_AMP_RPC_FAIL;
    }

    esp_amp_rpc_pkt_digest_t req_pkt_digest = {
        .client_addr = src_addr,
        .pkt_len = data_len,
        .pkt = req_pkt,
    };

    /* send request to server task. if queue is full or queue is NULL, destroy rpmsg message */
    if (server_inst->queue == NULL || (esp_amp_env_queue_send(server_inst->queue, &req_pkt_digest, 0) != 0)) {
        esp_amp_rpmsg_destroy(server_inst->rpmsg_dev, data);
    }
    return ESP_AMP_RPC_OK;
}

int esp_amp_rpc_server_run(esp_amp_rpc_server_t server, uint32_t timeout_ms)
{
    esp_amp_rpc_server_inst_t *server_inst = (esp_amp_rpc_server_inst_t *)server;
    if (server_inst == NULL) {
        return ESP_AMP_RPC_ERR_INVALID_ARG;
    }

    if (server_inst->running == false) {
        return ESP_AMP_RPC_ERR_INVALID_STATE;
    }

    /* recv request */
    esp_amp_rpc_pkt_digest_t req_pkt_digest;
    if (server_inst->queue && (esp_amp_env_queue_recv(server_inst->queue, &req_pkt_digest, timeout_ms) == 0)) {
        esp_amp_rpc_pkt_t *req_pkt = req_pkt_digest.pkt;
        exec_cmd_and_send(server_inst, req_pkt, req_pkt_digest.client_addr);
    }

    return ESP_AMP_RPC_OK;
}
#else
static int server_cb(void* data, uint16_t data_len, uint16_t src_addr, void* priv_data)
{
    esp_amp_rpc_server_inst_t *server_inst = (esp_amp_rpc_server_inst_t *)priv_data;
    esp_amp_rpc_pkt_t *req_pkt = (esp_amp_rpc_pkt_t *)data;
    if (server_inst == NULL || server_inst->running == false || req_pkt == NULL) {
        return ESP_AMP_RPC_FAIL;
    }

    /* execute inplace */
    exec_cmd_and_send(server_inst, req_pkt, src_addr);
    return ESP_AMP_RPC_OK;
}
#endif
