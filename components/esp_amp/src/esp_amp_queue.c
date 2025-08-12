/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_attr.h"

#include "esp_amp_queue.h"
#include "esp_amp_sys_info.h"
#include "esp_amp_sw_intr.h"
#include "esp_amp_platform.h"
#include "esp_amp_utils_priv.h"
#include "esp_amp_pm.h"

int IRAM_ATTR esp_amp_queue_send_try(esp_amp_queue_t *queue, void *data, uint16_t size)
{
    esp_err_t ret = ESP_OK;

    if (!queue->master) {
        // can only be called on `master-core`
        ret = ESP_ERR_NOT_SUPPORTED;
        goto exit;
    }

    if (queue->used_index == queue->free_index) {
        // send before alloc!
        ret = ESP_ERR_NOT_ALLOWED;
        goto exit;
    }
    if (queue->max_item_size < size) {
        // exceeds max size
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }

    uint16_t q_idx = queue->used_index & (queue->size - 1);
    uint16_t flags = queue->desc[q_idx].flags;
    esp_amp_platform_memory_barrier();
    if (!ESP_AMP_QUEUE_FLAG_IS_USED(queue->used_flip_counter, flags)) {
        // no free buffer slot to use, send fail, this should not happen
        ret = ESP_ERR_NOT_ALLOWED;
        goto exit;
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
        ret = queue->notify_fc(queue->priv_data);
    }

exit:
    /* NOTE: pm lock release for `alloc/send` pair */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();
    return ret;
}

int IRAM_ATTR esp_amp_queue_recv_try(esp_amp_queue_t *queue, void **buffer, uint16_t *size)
{
    esp_err_t ret = ESP_OK;
    *buffer = NULL;
    *size = 0;

    if (queue->master) {
        // can only be called on `remote-core`
        ret = ESP_ERR_NOT_SUPPORTED;
        goto exit;
    }

    uint16_t q_idx = queue->free_index & (queue->size - 1);
    /* vring is on RTC RAM, so no need to protect it from light sleep */
    uint16_t flags = queue->desc[q_idx].flags;
    esp_amp_platform_memory_barrier();

    if (!ESP_AMP_QUEUE_FLAG_IS_AVAILABLE(queue->free_flip_counter, flags)) {
        // no available buffer slot to receive, receive fail
        ret = ESP_ERR_NOT_FOUND;
        goto exit;
    }

    /* NOTE: pm lock acquire for `recv/free` pair */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();

    *buffer = (void *)(queue->desc[q_idx].addr);
    *size = queue->desc[q_idx].len;
    // make sure the buffer address and size are read and saved before returning
    queue->free_index += 1;

    if (q_idx == queue->size - 1) {
        // update the filp_counter if necessary
        queue->free_flip_counter = !queue->free_flip_counter;
    }

exit:
    return ret;
}

int IRAM_ATTR esp_amp_queue_alloc_try(esp_amp_queue_t *queue, void **buffer, uint16_t size)
{
    /* NOTE: pm lock acquire for `alloc/send` pair */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();

    esp_err_t ret = ESP_OK;
    *buffer = NULL;

    if (!queue->master) {
        // can only be called on `master-core`
        ret = ESP_ERR_NOT_SUPPORTED;
        goto exit;
    }

    if (queue->max_item_size < size) {
        // exceeds max size
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }

    uint16_t q_idx = queue->free_index & (queue->size - 1);
    uint16_t flags = queue->desc[q_idx].flags;
    esp_amp_platform_memory_barrier();
    if (!ESP_AMP_QUEUE_FLAG_IS_USED(queue->free_flip_counter, flags)) {
        // no available buffer slot to alloc, alloc fail
        ret = ESP_ERR_NOT_FOUND;
        goto exit;
    }

    *buffer = (void *)(queue->desc[q_idx].addr);
    queue->free_index += 1;

    if (q_idx == queue->size - 1) {
        // update the filp_counter if necessary
        queue->free_flip_counter = !queue->free_flip_counter;
    }

exit:
    if (ret != ESP_OK) {
        ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();
    }
    return ret;
}

int IRAM_ATTR esp_amp_queue_free_try(esp_amp_queue_t *queue, void *buffer)
{
    esp_err_t ret = ESP_OK;

    if (queue->master) {
        // can only be called on `remote-core`
        ret = ESP_ERR_NOT_SUPPORTED;
        goto exit;
    }

    if (queue->used_index == queue->free_index) {
        // free before receive!
        ret = ESP_ERR_NOT_ALLOWED;
        goto exit;
    }

    uint16_t q_idx = queue->used_index & (queue->size - 1);
    uint16_t flags = queue->desc[q_idx].flags;
    esp_amp_platform_memory_barrier();
    if (!ESP_AMP_QUEUE_FLAG_IS_AVAILABLE(queue->used_flip_counter, flags)) {
        // no available buffer slot to place freed buffer, free fail, this should not happen
        ret = ESP_ERR_NOT_ALLOWED;
        goto exit;
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

exit:
    /* NOTE: pm lock release for `recv/free` pair */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();
    return ret;
}

int esp_amp_queue_init_buffer(esp_amp_queue_conf_t *queue_conf, uint16_t queue_len, uint16_t queue_item_size,
                              esp_amp_queue_desc_t *queue_desc, void *queue_buffer)
{
    /* NOTE: pm lock for sys_info allocated `queue_conf` */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();

    queue_conf->queue_size = queue_len;
    queue_conf->max_queue_item_size = queue_item_size;
    queue_conf->queue_desc = queue_desc;
    queue_conf->queue_buffer = queue_buffer;
    uint8_t *_queue_buffer = (uint8_t *)queue_buffer;
    for (uint16_t desc_idx = 0; desc_idx < queue_conf->queue_size; desc_idx++) {
        queue_conf->queue_desc[desc_idx].addr = (uint32_t)_queue_buffer;
        queue_conf->queue_desc[desc_idx].flags = 0;
        queue_conf->queue_desc[desc_idx].len = queue_item_size;
        _queue_buffer += queue_item_size;
    }

    /* NOTE: pm lock for sys_info allocated `queue_conf` */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();
    return ESP_OK;
}

int esp_amp_queue_create(esp_amp_queue_t *queue, esp_amp_queue_conf_t *queue_conf, esp_amp_queue_cb_t cb_func,
                         void *priv_data, bool is_master)
{
    /* NOTE: pm lock for sys_info allocated `queue_conf` */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();

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

    /* NOTE: pm lock for sys_info allocated `queue_conf` */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();
    return ESP_OK;
}

#if IS_MAIN_CORE
int esp_amp_queue_main_init(esp_amp_queue_t *queue, uint16_t queue_len, uint16_t queue_item_size,
                            esp_amp_queue_cb_t cb_func, void *priv_data, bool is_master,
                            esp_amp_sys_info_id_t sysinfo_id)
{
    // force to ceil the queue length to power of 2
    uint16_t aligned_queue_len = get_power_len(queue_len);
    // force to align the queue item size with word boundary
    uint16_t aligned_queue_item_size = get_aligned_size(queue_item_size);

    if (aligned_queue_len == 0 || aligned_queue_item_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_amp_queue_conf_t *vq_config = NULL;
    void *vq_data_buffer = NULL;
    esp_amp_queue_desc_t *vq_desc = NULL;

#if CONFIG_ESP_AMP_SYSTEM_AUTO_LIGHT_SLEEP_SUPPORT_ENABLE
    if (is_master) {
        size_t vq_hp_buffer_size = sizeof(esp_amp_queue_conf_t) + aligned_queue_item_size * aligned_queue_len;
        uint8_t *vq_hp_buffer = (uint8_t *)(esp_amp_sys_info_alloc(sysinfo_id, vq_hp_buffer_size, SYS_INFO_CAP_HP));
        if (vq_hp_buffer == NULL) {
            // reserve memory not enough or corresponding sys_info already occupied
            return ESP_ERR_NO_MEM;
        }

        vq_config = (esp_amp_queue_conf_t *)(vq_hp_buffer);
        vq_hp_buffer += sizeof(esp_amp_queue_conf_t);
        vq_data_buffer = (void *)(vq_hp_buffer);

        size_t vq_rtc_buffer_size = sizeof(esp_amp_queue_desc_t) * aligned_queue_len;
        uint8_t *vq_rtc_buffer = (uint8_t *)(esp_amp_sys_info_alloc(sysinfo_id, vq_rtc_buffer_size, SYS_INFO_CAP_RTC));
        if (vq_rtc_buffer == NULL) {
            // reserve memory not enough or corresponding sys_info already occupied
            return ESP_ERR_NO_MEM;
        }

        vq_desc = (esp_amp_queue_desc_t *)(vq_rtc_buffer);
    } else {
#endif
        size_t vq_buffer_size = sizeof(esp_amp_queue_conf_t) + sizeof(esp_amp_queue_desc_t) * aligned_queue_len +
                                aligned_queue_item_size * aligned_queue_len;
        uint8_t *vq_buffer = (uint8_t *)(esp_amp_sys_info_alloc(sysinfo_id, vq_buffer_size, SYS_INFO_CAP_HP));
        if (vq_buffer == NULL) {
            // reserve memory not enough or corresponding sys_info already occupied
            return ESP_ERR_NO_MEM;
        }

        vq_config = (esp_amp_queue_conf_t *)(vq_buffer);
        vq_buffer += sizeof(esp_amp_queue_conf_t);
        vq_desc = (esp_amp_queue_desc_t *)(vq_buffer);
        vq_buffer += sizeof(esp_amp_queue_desc_t) * aligned_queue_len;
        vq_data_buffer = (void *)(vq_buffer);
#if CONFIG_ESP_AMP_SYSTEM_AUTO_LIGHT_SLEEP_SUPPORT_ENABLE
    }
#endif

    esp_amp_queue_init_buffer(vq_config, aligned_queue_len, aligned_queue_item_size, vq_desc, vq_data_buffer);
    esp_amp_queue_create(queue, vq_config, cb_func, priv_data, is_master);

    return ESP_OK;
}
#else  /* !IS_MAIN_CORE */
int esp_amp_queue_sub_init(esp_amp_queue_t *queue, esp_amp_queue_cb_t cb_func, void *priv_data, bool is_master,
                           esp_amp_sys_info_id_t sysinfo_id)
{
    uint16_t queue_shm_size;
    uint8_t *vq_buffer = esp_amp_sys_info_get(sysinfo_id, &queue_shm_size, SYS_INFO_CAP_HP);

    if (vq_buffer == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_amp_queue_conf_t *vq_config = (esp_amp_queue_conf_t *)(vq_buffer);

    esp_amp_queue_create(queue, vq_config, cb_func, priv_data, is_master);

    return ESP_OK;
}
#endif /* IS_MAIN_CORE */

int esp_amp_queue_intr_enable(esp_amp_queue_t *queue, esp_amp_sw_intr_id_t sw_intr_id)
{
    /* NOTE: pm lock for `esp_amp_rpmsg_intr_enable` */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_ENTER();

    esp_err_t ret = ESP_OK;

    if (queue->master) {
        /* should only be called on `remote-core` */
        ret = ESP_ERR_NOT_SUPPORTED;
        goto exit;
    }

    if (esp_amp_sw_intr_add_handler(sw_intr_id, queue->callback_fc, queue->priv_data) != 0) {
        ret = ESP_ERR_NOT_FINISHED;
        goto exit;
    }

exit:
    /* NOTE: pm lock for `esp_amp_rpmsg_intr_enable` */
    ESP_AMP_PM_SKIP_LIGHT_SLEEP_EXIT();
    return ret;
}
