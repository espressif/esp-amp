/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#include "sdkconfig.h"
#include "esp_attr.h"

#include "esp_amp_queue.h"
#include "esp_amp_sys_info.h"
#include "esp_amp_sw_intr.h"
#include "esp_amp_platform.h"
#include "esp_amp_utils_priv.h"

int IRAM_ATTR esp_amp_queue_send_try(esp_amp_queue_t *queue, void* data, uint16_t size)
{
    if (!queue->master) {
        // can only be called on `master-core`
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (queue->used_index == queue->free_index) {
        // send before alloc!
        return ESP_ERR_NOT_ALLOWED;
    }
    if (queue->max_item_size < size) {
        // exceeds max size
        return ESP_ERR_NO_MEM;
    }

    uint16_t q_idx = queue->used_index & (queue->size - 1);
    uint16_t flags = queue->desc[q_idx].flags;
    esp_amp_platform_memory_barrier();
    if (!ESP_AMP_QUEUE_FLAG_IS_USED(queue->used_flip_counter, flags)) {
        // no free buffer slot to use, send fail, this should not happen
        return ESP_ERR_NOT_ALLOWED;
    }

    queue->desc[q_idx].addr = (uint32_t)(data);
    queue->desc[q_idx].len = size;
    esp_amp_platform_memory_barrier();
    // make sure the buffer address and size are set before making the slot available to use
    queue->used_index += 1;
    queue->desc[q_idx].flags ^= ESP_AMP_QUEUE_AVAILABLE_MASK(1);
    /*
        Since we confirm that ESP_AMP_QUEUE_FLAG_IS_USED is true, so at this moment, AVAILABLE flag should be different from the flip_counter.
        To set the AVAILABLE flag the same as the flip_counter, we just XOR the corresponding bit with 1, which will make it equal to the flip_counter.
    */
    if (q_idx == queue->size - 1) {
        // update the filp_counter if necessary
        queue->used_flip_counter = !queue->used_flip_counter;
    }

    // notify the opposite side if necessary
    if (queue->notify_fc != NULL) {
        return queue->notify_fc(queue->priv_data);
    }

    return ESP_OK;
}

int IRAM_ATTR esp_amp_queue_recv_try(esp_amp_queue_t *queue, void** buffer, uint16_t* size)
{
    *buffer = NULL;
    *size = 0;
    if (queue->master) {
        // can only be called on `remote-core`
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint16_t q_idx = queue->free_index & (queue->size - 1);
    uint16_t flags = queue->desc[q_idx].flags;
    esp_amp_platform_memory_barrier();

    if (!ESP_AMP_QUEUE_FLAG_IS_AVAILABLE(queue->free_flip_counter, flags)) {
        // no available buffer slot to receive, receive fail
        return ESP_ERR_NOT_FOUND;
    }

    *buffer = (void*)(queue->desc[q_idx].addr);
    *size = queue->desc[q_idx].len;
    // make sure the buffer address and size are read and saved before returning
    queue->free_index += 1;

    if (q_idx == queue->size - 1) {
        // update the filp_counter if necessary
        queue->free_flip_counter = !queue->free_flip_counter;
    }

    return ESP_OK;
}

int IRAM_ATTR esp_amp_queue_alloc_try(esp_amp_queue_t *queue, void** buffer, uint16_t size)
{
    *buffer = NULL;
    if (!queue->master) {
        // can only be called on `master-core`
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (queue->max_item_size < size) {
        // exceeds max size
        return ESP_ERR_NO_MEM;
    }

    uint16_t q_idx = queue->free_index & (queue->size - 1);
    uint16_t flags = queue->desc[q_idx].flags;
    esp_amp_platform_memory_barrier();
    if (!ESP_AMP_QUEUE_FLAG_IS_USED(queue->free_flip_counter, flags)) {
        // no available buffer slot to alloc, alloc fail
        return ESP_ERR_NOT_FOUND;
    }

    *buffer = (void*)(queue->desc[q_idx].addr);
    queue->free_index += 1;

    if (q_idx == queue->size - 1) {
        // update the filp_counter if necessary
        queue->free_flip_counter = !queue->free_flip_counter;
    }

    return ESP_OK;
}

int IRAM_ATTR esp_amp_queue_free_try(esp_amp_queue_t *queue, void* buffer)
{
    if (queue->master) {
        // can only be called on `remote-core`
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (queue->used_index == queue->free_index) {
        // free before receive!
        return ESP_ERR_NOT_ALLOWED;
    }

    uint16_t q_idx = queue->used_index & (queue->size - 1);
    uint16_t flags = queue->desc[q_idx].flags;
    esp_amp_platform_memory_barrier();
    if (!ESP_AMP_QUEUE_FLAG_IS_AVAILABLE(queue->used_flip_counter, flags)) {
        // no available buffer slot to place freed buffer, free fail, this should not happen
        return ESP_ERR_NOT_ALLOWED;
    }

    queue->desc[q_idx].addr = (uint32_t)(buffer);
    queue->desc[q_idx].len = queue->max_item_size;
    esp_amp_platform_memory_barrier();
    // make sure the buffer address and size are set before making the slot available to use
    queue->used_index += 1;
    queue->desc[q_idx].flags ^= ESP_AMP_QUEUE_USED_MASK(1);
    /*
        Since we confirm that ESP_AMP_QUEUE_FLAG_IS_AVAILABLE is true, so at this moment, USED flag should be different from the flip_counter.
        To set the USED flag the same as the flip_counter, we just XOR the corresponding bit with 1, which will make it equal to the flip_counter.
    */
    if (q_idx == queue->size - 1) {
        // update the filp_counter if necessary
        queue->used_flip_counter = !queue->used_flip_counter;
    }

    return ESP_OK;
}

int esp_amp_queue_init_buffer(esp_amp_queue_conf_t* queue_conf, uint16_t queue_len, uint16_t queue_item_size, esp_amp_queue_desc_t* queue_desc, void* queue_buffer)
{
    queue_conf->queue_size = queue_len;
    queue_conf->max_queue_item_size = queue_item_size;
    queue_conf->queue_desc = queue_desc;
    queue_conf->queue_buffer = queue_buffer;
    uint8_t* _queue_buffer = (uint8_t*)queue_buffer;
    for (uint16_t desc_idx = 0; desc_idx < queue_conf->queue_size; desc_idx++) {
        queue_conf->queue_desc[desc_idx].addr = (uint32_t)_queue_buffer;
        queue_conf->queue_desc[desc_idx].flags = 0;
        queue_conf->queue_desc[desc_idx].len = queue_item_size;
        _queue_buffer += queue_item_size;
    }
    return ESP_OK;
}

int esp_amp_queue_create(esp_amp_queue_t* queue, esp_amp_queue_conf_t* queue_conf, esp_amp_queue_cb_t cb_func, void* priv_data, bool is_master)
{
    queue->size = queue_conf->queue_size;
    queue->desc = queue_conf->queue_desc;
    queue->free_flip_counter = 1;
    queue->used_flip_counter = 1;
    queue->free_index = 0;
    queue->used_index = 0;
    queue->max_item_size = queue_conf->max_queue_item_size;
    if (is_master) {
        /* master can only send message */
        queue->notify_fc = cb_func;
        queue->callback_fc = NULL;
    } else {
        /* remote can only receive message */
        queue->notify_fc = NULL;
        queue->callback_fc = cb_func;
    }

    queue->priv_data = priv_data;
    queue->master = is_master;
    return ESP_OK;
}

#if IS_MAIN_CORE
int esp_amp_queue_main_init(esp_amp_queue_t* queue, uint16_t queue_len, uint16_t queue_item_size, esp_amp_queue_cb_t cb_func, void* priv_data, bool is_master, esp_amp_sys_info_id_t sysinfo_id)
{

    // force to ceil the queue length to power of 2
    uint16_t aligned_queue_len = get_power_len(queue_len);
    // force to align the queue item size with word boundary
    uint16_t aligned_queue_item_size = get_aligned_size(queue_item_size);

    if (aligned_queue_len == 0 || aligned_queue_item_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t queue_shm_size = sizeof(esp_amp_queue_conf_t) + sizeof(esp_amp_queue_desc_t) * aligned_queue_len + aligned_queue_item_size * aligned_queue_len;

    uint8_t* vq_buffer = (uint8_t*)(esp_amp_sys_info_alloc(sysinfo_id, queue_shm_size, SYS_INFO_CAP_HP));
    if (vq_buffer == NULL) {
        // reserve memory not enough or corresponding sys_info already occupied
        return ESP_ERR_NO_MEM;
    }

    esp_amp_queue_conf_t* vq_confg = (esp_amp_queue_conf_t*)(vq_buffer);
    vq_buffer += sizeof(esp_amp_queue_conf_t);
    esp_amp_queue_desc_t* vq_desc = (esp_amp_queue_desc_t*)(vq_buffer);
    vq_buffer += sizeof(esp_amp_queue_desc_t) * aligned_queue_len;
    void* vq_data_buffer = (void*)(vq_buffer);

    esp_amp_queue_init_buffer(vq_confg, aligned_queue_len, aligned_queue_item_size, vq_desc, vq_data_buffer);
    esp_amp_queue_create(queue, vq_confg, cb_func, priv_data, is_master);

    return ESP_OK;
}
#endif

int esp_amp_queue_sub_init(esp_amp_queue_t* queue, esp_amp_queue_cb_t cb_func, void* priv_data, bool is_master, esp_amp_sys_info_id_t sysinfo_id)
{
    uint16_t queue_shm_size;
    uint8_t* vq_buffer = esp_amp_sys_info_get(sysinfo_id, &queue_shm_size, SYS_INFO_CAP_HP);

    if (vq_buffer == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_amp_queue_conf_t* vq_confg = (esp_amp_queue_conf_t*)(vq_buffer);

    esp_amp_queue_create(queue, vq_confg, cb_func, priv_data, is_master);

    return ESP_OK;
}

int esp_amp_queue_intr_enable(esp_amp_queue_t* queue, esp_amp_sw_intr_id_t sw_intr_id)
{
    if (queue->master) {
        /* should only be called on `remote-core` */
        return ESP_ERR_NOT_SUPPORTED;
    }

    int ret = esp_amp_sw_intr_add_handler(sw_intr_id, queue->callback_fc, queue->priv_data);

    if (ret != 0) {
        return ESP_ERR_NOT_FINISHED;
    }

    return ESP_OK;
}
