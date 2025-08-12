/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#include "esp_attr.h"

#include "esp_amp_env.h"
#include "esp_amp_utils_priv.h"
#include "esp_amp_rpmsg.h"
#include "esp_amp_platform.h"
#include "esp_amp_sys_info.h"
#include "esp_amp_sw_intr.h"

static void __esp_amp_rpmsg_extend_endpoint_list(esp_amp_rpmsg_ept_t** ept_head, esp_amp_rpmsg_ept_t* new_ept)
{
    if (*ept_head == NULL) {
        *ept_head = new_ept;
        new_ept->next_ept = NULL;
    } else {
        // add new endpoint to the head of the linked list
        new_ept->next_ept = *ept_head;
        *ept_head = new_ept;
    }
}

static esp_amp_rpmsg_ept_t* IRAM_ATTR __esp_amp_rpmsg_search_endpoint(esp_amp_rpmsg_dev_t* rpmsg_device, uint16_t ept_addr)
{
    // empty endpoint list
    if (rpmsg_device->ept_list == NULL) {
        return NULL;
    }

    // iterate over endpoint linked list to search for the given endpoint list
    for (esp_amp_rpmsg_ept_t * ept_ptr = rpmsg_device->ept_list; ept_ptr != NULL; ept_ptr = ept_ptr->next_ept) {
        if (ept_ptr->addr == ept_addr) {
            return ept_ptr;
        }
    }

    return NULL;
}

esp_amp_rpmsg_ept_t* esp_amp_rpmsg_search_endpoint(esp_amp_rpmsg_dev_t* rpmsg_device, uint16_t ept_addr)
{
    esp_amp_rpmsg_ept_t* ept_ptr;

    esp_amp_env_enter_critical();

    ept_ptr = __esp_amp_rpmsg_search_endpoint(rpmsg_device, ept_addr);

    esp_amp_env_exit_critical();

    return ept_ptr;
}

esp_amp_rpmsg_ept_t* esp_amp_rpmsg_create_endpoint(esp_amp_rpmsg_dev_t* rpmsg_device, uint16_t ept_addr, esp_amp_ept_cb_t ept_rx_cb, void* ept_rx_cb_data, esp_amp_rpmsg_ept_t* ept_ctx)
{
    if (ept_ctx == NULL) {
        // invalid endpoint context
        return NULL;
    }

    esp_amp_env_enter_critical();

    if (__esp_amp_rpmsg_search_endpoint(rpmsg_device, ept_addr) != NULL) {
        // endpoint address already exist!
        esp_amp_env_exit_critical();
        return NULL;
    }

    ept_ctx->addr = ept_addr;
    ept_ctx->rx_cb = ept_rx_cb;
    ept_ctx->rx_cb_data = ept_rx_cb_data;
    __esp_amp_rpmsg_extend_endpoint_list(&(rpmsg_device->ept_list), ept_ctx);

    esp_amp_env_exit_critical();

    return ept_ctx;
}

esp_amp_rpmsg_ept_t* esp_amp_rpmsg_delete_endpoint(esp_amp_rpmsg_dev_t* rpmsg_device, uint16_t ept_addr)
{

    esp_amp_env_enter_critical();

    esp_amp_rpmsg_ept_t* cur_ept = rpmsg_device->ept_list;
    esp_amp_rpmsg_ept_t* prev_ept = NULL;

    while (cur_ept != NULL) {
        if (cur_ept->addr == ept_addr) {
            break;
        }
        prev_ept = cur_ept;
        cur_ept = cur_ept->next_ept;
    }

    if (cur_ept == NULL) {
        // endpoint address not exist!
        esp_amp_env_exit_critical();
        return NULL;
    }

    if (prev_ept == NULL) {
        // delete the head endpoint
        rpmsg_device->ept_list = cur_ept->next_ept;
    } else {
        prev_ept->next_ept = cur_ept->next_ept;
    }
    cur_ept->next_ept = NULL;

    esp_amp_env_exit_critical();

    return cur_ept;
}

esp_amp_rpmsg_ept_t* esp_amp_rpmsg_rebind_endpoint(esp_amp_rpmsg_dev_t* rpmsg_device, uint16_t ept_addr, esp_amp_ept_cb_t ept_rx_cb, void* ept_rx_cb_data)
{

    esp_amp_env_enter_critical();

    esp_amp_rpmsg_ept_t* ept_ptr = __esp_amp_rpmsg_search_endpoint(rpmsg_device, ept_addr);
    if (ept_ptr == NULL) {
        // endpoint address not exist!
        esp_amp_env_exit_critical();
        return NULL;
    }

    ept_ptr->rx_cb = ept_rx_cb;
    ept_ptr->rx_cb_data = ept_rx_cb_data;

    esp_amp_env_exit_critical();

    return ept_ptr;
}

static int IRAM_ATTR __esp_amp_rpmsg_dispatcher(esp_amp_rpmsg_t* rpmsg, esp_amp_rpmsg_dev_t* rpmsg_dev)
{
    esp_amp_rpmsg_ept_t* ept = __esp_amp_rpmsg_search_endpoint(rpmsg_dev, rpmsg->msg_head.dst_addr);
    if (ept == NULL) {
        // can't find endpoint, ignore and return
        return -1;
    }

    if (ept->rx_cb == NULL) {
        // endpoint has no callback function, nothing to do
        return 0;
    }
    ept->rx_cb((void*)(rpmsg->msg_data), rpmsg->msg_head.data_len, rpmsg->msg_head.src_addr, ept->rx_cb_data);
    return 0;
}

int IRAM_ATTR esp_amp_rpmsg_poll(esp_amp_rpmsg_dev_t* rpmsg_dev)
{
    esp_amp_rpmsg_t* rpmsg;
    uint16_t rpmsg_size;
    if (rpmsg_dev->queue_ops.q_rx(rpmsg_dev->rx_queue, (void * *)(&rpmsg), &rpmsg_size) != 0) {
        // nothing to receive
        return -1;
    }

    return __esp_amp_rpmsg_dispatcher(rpmsg, rpmsg_dev);
}

static int IRAM_ATTR __esp_amp_rpmsg_rx_callback(void* data)
{
    esp_amp_rpmsg_dev_t* rpmsg_dev = (esp_amp_rpmsg_dev_t*) data;
    while (esp_amp_rpmsg_poll(rpmsg_dev) == 0) {
        // receive and process all avaialble vqueue item
    }
    return 0;
}

static int IRAM_ATTR __esp_amp_rpmsg_tx_notify(void* data)
{
    esp_amp_sw_intr_trigger(SW_INTR_RESERVED_ID_RPMSG);
    return 0;
}

int esp_amp_rpmsg_intr_enable(esp_amp_rpmsg_dev_t* rpmsg_dev)
{
    return esp_amp_queue_intr_enable(rpmsg_dev->rx_queue, SW_INTR_RESERVED_ID_RPMSG);
}

static void __esp_amp_rpmsg_dev_init(esp_amp_rpmsg_dev_t* rpmsg_dev, esp_amp_queue_t vqueue[])
{
    rpmsg_dev->tx_queue = &vqueue[0];
    rpmsg_dev->rx_queue = &vqueue[1];
    rpmsg_dev->ept_list = NULL;
    rpmsg_dev->queue_ops.q_tx = esp_amp_queue_send_try;
    rpmsg_dev->queue_ops.q_tx_alloc = esp_amp_queue_alloc_try;
    rpmsg_dev->queue_ops.q_rx = esp_amp_queue_recv_try;
    rpmsg_dev->queue_ops.q_rx_free = esp_amp_queue_free_try;
}

#if IS_MAIN_CORE
int esp_amp_rpmsg_main_init_by_id(esp_amp_rpmsg_dev_t* rpmsg_dev, esp_amp_queue_t rpmsg_vqueue[], uint16_t queue_len, uint16_t queue_item_size, bool notify, bool poll, esp_amp_sys_info_id_t sysinfo_id)
{
    // force to ceil the queue length to power of 2
    uint16_t aligned_queue_len = get_power_len(queue_len);
    // force to align the queue item size with word boundary
    uint16_t aligned_queue_item_size = get_aligned_size(queue_item_size);

    if (aligned_queue_len == 0 || aligned_queue_item_size == 0) {
        return -1;
    }

    esp_amp_queue_cb_t tx_notify = notify ? __esp_amp_rpmsg_tx_notify : NULL;
    esp_amp_queue_cb_t rx_callback = poll ? NULL : __esp_amp_rpmsg_rx_callback;

    size_t queue_shm_size = 2 * (sizeof(esp_amp_queue_conf_t) + sizeof(esp_amp_queue_desc_t) * aligned_queue_len + aligned_queue_item_size * aligned_queue_len);
    // alloc fixed-size buffer for TX/RX Virtqueue
    uint8_t* vq_buffer = (uint8_t*)(esp_amp_sys_info_alloc(sysinfo_id, queue_shm_size, SYS_INFO_CAP_HP));
    if (vq_buffer == NULL) {
        // reserve memory not enough or corresponding sys_info already occupied
        return -1;
    }

    esp_amp_queue_conf_t* vq_tx_confg = (esp_amp_queue_conf_t*)(vq_buffer);
    vq_buffer += sizeof(esp_amp_queue_conf_t);
    esp_amp_queue_conf_t* vq_rx_confg = (esp_amp_queue_conf_t*)(vq_buffer);
    vq_buffer += sizeof(esp_amp_queue_conf_t);
    esp_amp_queue_desc_t* vq_tx_desc = (esp_amp_queue_desc_t*)(vq_buffer);
    vq_buffer += sizeof(esp_amp_queue_desc_t) * aligned_queue_len;
    esp_amp_queue_desc_t* vq_rx_desc = (esp_amp_queue_desc_t*)(vq_buffer);
    vq_buffer += sizeof(esp_amp_queue_desc_t) * aligned_queue_len;
    void* vq_tx_data_buffer = (void*)(vq_buffer);
    vq_buffer += aligned_queue_item_size * aligned_queue_len;
    void* vq_rx_data_buffer = (void*)(vq_buffer);

    // initialize the queue config
    esp_amp_queue_init_buffer(vq_tx_confg, aligned_queue_len, aligned_queue_item_size, vq_tx_desc, vq_tx_data_buffer);
    esp_amp_queue_init_buffer(vq_rx_confg, aligned_queue_len, aligned_queue_item_size, vq_rx_desc, vq_rx_data_buffer);
    // initialize the local queue structure
    esp_amp_queue_create(&rpmsg_vqueue[0], vq_tx_confg, tx_notify, (void*)(rpmsg_dev), true);
    esp_amp_queue_create(&rpmsg_vqueue[1], vq_rx_confg, rx_callback, (void*)(rpmsg_dev), false);

    __esp_amp_rpmsg_dev_init(rpmsg_dev, rpmsg_vqueue);

    return 0;
}

int esp_amp_rpmsg_main_init(esp_amp_rpmsg_dev_t* rpmsg_dev, uint16_t queue_len, uint16_t queue_item_size, bool notify, bool poll)
{
    static esp_amp_queue_t vqueue[2];
    return esp_amp_rpmsg_main_init_by_id(rpmsg_dev, vqueue, queue_len, queue_item_size, notify, poll, SYS_INFO_RESERVED_ID_VQUEUE);
}
#else
int esp_amp_rpmsg_sub_init_by_id(esp_amp_rpmsg_dev_t* rpmsg_dev, esp_amp_queue_t rpmsg_vqueue[], bool notify, bool poll, esp_amp_sys_info_id_t sysinfo_id)
{
    uint16_t queue_shm_size;
    uint8_t* vq_buffer = esp_amp_sys_info_get(sysinfo_id, &queue_shm_size, SYS_INFO_CAP_HP);

    if (vq_buffer == NULL) {
        return -1;
    }

    esp_amp_queue_cb_t tx_notify = notify ? __esp_amp_rpmsg_tx_notify : NULL;
    esp_amp_queue_cb_t rx_callback = poll ? NULL : __esp_amp_rpmsg_rx_callback;

    // Note: the configuration is different from the queue_main_init, since the main TX is sub RX; main RX is sub TX;
    esp_amp_queue_conf_t* vq_tx_confg = (esp_amp_queue_conf_t*)(vq_buffer + sizeof(esp_amp_queue_conf_t));
    esp_amp_queue_conf_t* vq_rx_confg = (esp_amp_queue_conf_t*)(vq_buffer);
    // initialize the local queue structure
    esp_amp_queue_create(&rpmsg_vqueue[0], vq_tx_confg, tx_notify, (void*)(rpmsg_dev), true);
    esp_amp_queue_create(&rpmsg_vqueue[1], vq_rx_confg, rx_callback, (void*)(rpmsg_dev), false);

    __esp_amp_rpmsg_dev_init(rpmsg_dev, rpmsg_vqueue);

    return 0;
}

int esp_amp_rpmsg_sub_init(esp_amp_rpmsg_dev_t* rpmsg_dev, bool notify, bool poll)
{
    static esp_amp_queue_t vqueue[2];
    return esp_amp_rpmsg_sub_init_by_id(rpmsg_dev, vqueue, notify, poll, SYS_INFO_RESERVED_ID_VQUEUE);
}
#endif

void* esp_amp_rpmsg_create_message(esp_amp_rpmsg_dev_t* rpmsg_dev, uint32_t nbytes, uint16_t flags)
{
    uint32_t rpmsg_size = nbytes + offsetof(esp_amp_rpmsg_t, msg_data);
    esp_amp_rpmsg_t* rpmsg;
    if (rpmsg_size >= (uint32_t)(1) << 16) {
        return NULL;
    }

    esp_amp_env_enter_critical();

    int ret = rpmsg_dev->queue_ops.q_tx_alloc(rpmsg_dev->tx_queue, (void**)(&rpmsg), rpmsg_size);

    esp_amp_env_exit_critical();

    if (rpmsg == NULL || ret == -1) {
        return NULL;
    }

    rpmsg->msg_head.data_flags = flags;
    rpmsg->msg_head.data_len = nbytes;

    return (void*)((uint8_t*)(rpmsg) + offsetof(esp_amp_rpmsg_t, msg_data));
}

int esp_amp_rpmsg_send(esp_amp_rpmsg_dev_t* rpmsg_dev, esp_amp_rpmsg_ept_t* ept, uint16_t dst_addr, void* data, uint16_t data_len)
{

    if (data == NULL || data_len == 0) {
        return -1;
    }

    void* buffer = esp_amp_rpmsg_create_message(rpmsg_dev, data_len, ESP_AMP_RPMSG_DATA_DEFAULT);

    if (buffer == NULL) {
        return -1;
    }

    for (uint16_t i = 0; i < data_len; i++) {
        ((uint8_t*)(buffer))[i] = ((uint8_t*)(data))[i];
    }

    return esp_amp_rpmsg_send_nocopy(rpmsg_dev, ept, dst_addr, buffer, data_len);
}

int esp_amp_rpmsg_send_nocopy(esp_amp_rpmsg_dev_t* rpmsg_dev, esp_amp_rpmsg_ept_t* ept, uint16_t dst_addr, void* data, uint16_t data_len)
{
    esp_amp_rpmsg_t* rpmsg = (esp_amp_rpmsg_t*)((uint8_t*)(data) - offsetof(esp_amp_rpmsg_t, msg_data));
    rpmsg->msg_head.data_len = data_len;
    rpmsg->msg_head.dst_addr = dst_addr;
    rpmsg->msg_head.src_addr = ept->addr;

    esp_amp_env_enter_critical();

    int ret = rpmsg_dev->queue_ops.q_tx(rpmsg_dev->tx_queue, rpmsg, rpmsg_dev->tx_queue->max_item_size);

    esp_amp_env_exit_critical();

    return ret;
}

int esp_amp_rpmsg_destroy(esp_amp_rpmsg_dev_t* rpmsg_dev, void* msg_data)
{
    esp_amp_rpmsg_t* rpmsg = (esp_amp_rpmsg_t*)((uint8_t*)(msg_data) - offsetof(esp_amp_rpmsg_t, msg_data));

    esp_amp_env_enter_critical();

    int ret = rpmsg_dev->queue_ops.q_rx_free(rpmsg_dev->rx_queue, rpmsg);

    esp_amp_env_exit_critical();

    return ret;
}

uint16_t IRAM_ATTR esp_amp_rpmsg_get_max_size(esp_amp_rpmsg_dev_t* rpmsg_dev)
{
    return (uint16_t)(rpmsg_dev->tx_queue->max_item_size - offsetof(esp_amp_rpmsg_t, msg_data));
}
