/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_attr.h"
#include "esp_amp_log.h"
#include "esp_amp_env.h"
#include "esp_amp_rpc.h"

static const DRAM_ATTR __attribute__((unused)) char TAG[] = "esp_amp_rpc_client";

static int IRAM_ATTR client_cb(void* data, uint16_t data_len, uint16_t src_addr, void* priv_data)
{
    esp_amp_rpc_pkt_t *resp_pkt = (esp_amp_rpc_pkt_t *)data;
    esp_amp_rpc_client_inst_t *client_inst = (esp_amp_rpc_client_inst_t *)priv_data;
    if (client_inst == NULL || client_inst->running == false || resp_pkt == NULL) {
        return ESP_AMP_RPC_FAIL;
    }

    /* if response to current pending request, copy response data to response buffer */
    if (client_inst->pending_cmd != NULL && resp_pkt->msg_id == client_inst->pending_id) {
        client_inst->pending_cmd->status = resp_pkt->status;

        if (resp_pkt->msg_len > 0 && client_inst->pending_cmd->resp_data != NULL) {
            int cpy_len = resp_pkt->msg_len > client_inst->pending_cmd->resp_len ?
                          client_inst->pending_cmd->resp_len : resp_pkt->msg_len;
            memcpy(client_inst->pending_cmd->resp_data, resp_pkt->msg_data, cpy_len);
        }

        if (client_inst->pending_cmd->cb) {
            client_inst->pending_cmd->cb(client_inst, client_inst->pending_cmd, client_inst->pending_cmd->cb_arg);
        }
    }

    esp_amp_rpmsg_destroy(client_inst->rpmsg_dev, data);
    return ESP_AMP_RPC_OK;
}

esp_amp_rpc_client_t esp_amp_rpc_client_init(esp_amp_rpc_client_cfg_t *cfg)
{
    if (cfg == NULL || cfg->stg == NULL || cfg->rpmsg_dev == NULL) {
        return NULL;
    }

    esp_amp_rpc_client_inst_t *client_inst = (esp_amp_rpc_client_inst_t *)cfg->stg;
    esp_amp_rpmsg_ept_t *rpmsg_ept = esp_amp_rpmsg_create_endpoint(cfg->rpmsg_dev, cfg->client_id, client_cb, client_inst, &client_inst->rpmsg_ept);
    if (rpmsg_ept == NULL) {
        return NULL;
    }

    esp_amp_env_enter_critical();
    client_inst->rpmsg_dev = cfg->rpmsg_dev;
    client_inst->server_id = cfg->server_id;
    client_inst->client_id = cfg->client_id;
    client_inst->poll_arg = cfg->poll_arg;
    client_inst->poll_cb = cfg->poll_cb;
    client_inst->pending_cmd = NULL;
    client_inst->pending_id = 0;
    client_inst->running = true;
    esp_amp_env_exit_critical();
    return client_inst;
}

void esp_amp_rpc_client_deinit(esp_amp_rpc_client_t client)
{
    esp_amp_rpc_client_inst_t *client_inst = (esp_amp_rpc_client_inst_t *)client;
    if (client_inst == NULL || client_inst->running == false) {
        return;
    }

    esp_amp_env_enter_critical();
    esp_amp_rpmsg_delete_endpoint(client_inst->rpmsg_dev, client_inst->client_id);
    memset(client_inst, 0, sizeof(esp_amp_rpc_client_inst_t));
    esp_amp_env_exit_critical();
}

int esp_amp_rpc_client_execute_cmd(esp_amp_rpc_client_t client, esp_amp_rpc_cmd_t *cmd)
{
    esp_amp_rpc_client_inst_t *client_inst = (esp_amp_rpc_client_inst_t *)client;
    if (client_inst == NULL || cmd == NULL) {
        return ESP_AMP_RPC_ERR_INVALID_ARG;
    }

    if (client_inst->running == false) {
        return ESP_AMP_RPC_ERR_INVALID_STATE;
    }

    /* set cmd status to pending */
    cmd->status = ESP_AMP_RPC_STATUS_PENDING;

    /* fetch new buffer from buffer pool */
    if (cmd->req_len > UINT16_MAX - sizeof(esp_amp_rpc_pkt_t)) {
        return ESP_AMP_RPC_ERR_INVALID_SIZE;
    }
    uint16_t req_pkt_len = cmd->req_len + sizeof(esp_amp_rpc_pkt_t);
    uint8_t *req_pkt_buf = (uint8_t *)esp_amp_rpmsg_create_message(client_inst->rpmsg_dev, req_pkt_len, ESP_AMP_RPMSG_DATA_DEFAULT);
    if (req_pkt_buf == NULL) {
        if (esp_amp_rpmsg_get_max_size(client_inst->rpmsg_dev) < req_pkt_len) {
            return ESP_AMP_RPC_ERR_INVALID_SIZE; /* buffer cannot fit */
        }
        return ESP_AMP_RPC_ERR_NO_MEM; /* buffer pool is empty */
    }

    /* construct packet */
    esp_amp_rpc_pkt_t req_pkt = {
        .cmd_id = cmd->cmd_id,
        .status = ESP_AMP_RPC_STATUS_PENDING,
        .msg_id = ++client_inst->pending_id,
        .msg_len = cmd->req_len,
    };

    /* keep track of current command for later response */
    client_inst->pending_cmd = cmd;
    memcpy(req_pkt_buf, &req_pkt, sizeof(esp_amp_rpc_pkt_t));
    memcpy(req_pkt_buf + sizeof(esp_amp_rpc_pkt_t), cmd->req_data, cmd->req_len);

    /* send packet to server */
    esp_amp_rpmsg_send_nocopy(client_inst->rpmsg_dev, &client_inst->rpmsg_ept, client_inst->server_id, req_pkt_buf, req_pkt_len);
    return ESP_AMP_RPC_OK;
}

void esp_amp_rpc_client_poll(esp_amp_rpc_client_t client)
{
    esp_amp_rpc_client_inst_t *client_inst = (esp_amp_rpc_client_inst_t *)client;
    if (client_inst && client_inst->running && client_inst->poll_cb) {
        client_inst->poll_cb(client_inst->poll_arg);
    }
}
