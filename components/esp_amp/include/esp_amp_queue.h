/*
* SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "esp_err.h"

#include "esp_amp_sw_intr.h"
#include "esp_amp_sys_info.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_amp_queue_desc_t {
    uint32_t addr;
    uint16_t len;
    uint16_t flags;
} esp_amp_queue_desc_t;

typedef int (*esp_amp_queue_cb_t)(void*);
typedef struct esp_amp_queue_t {
    esp_amp_queue_desc_t* desc;
    uint16_t size;
    uint16_t free_index;
    uint16_t used_index;
    uint16_t max_item_size;
    bool master;
    esp_amp_queue_cb_t callback_fc;             /* This callback function will be called whenever the opposite side sends notification(interrupt) to us */
    esp_amp_queue_cb_t notify_fc;               /* This function is used to notify the opposite side (normally, send software interrupt) when necessary */
    void* priv_data;
    uint16_t free_flip_counter;
    uint16_t used_flip_counter;
} esp_amp_queue_t;

typedef struct esp_amp_queue_ops_t {
    int (*q_tx)(esp_amp_queue_t* queue, void* buffer, uint16_t size);
    int (*q_tx_alloc)(esp_amp_queue_t *queue, void** buffer, uint16_t size);
    int (*q_rx)(esp_amp_queue_t* queue, void** buffer, uint16_t* size);
    int (*q_rx_free)(esp_amp_queue_t *queue, void* buffer);
} esp_amp_queue_ops_t;

typedef struct esp_amp_queue_conf_t {
    uint16_t queue_size;
    uint16_t max_queue_item_size;
    uint8_t* queue_buffer;
    esp_amp_queue_desc_t* queue_desc;
} esp_amp_queue_conf_t;

/**
 * Try to send a data buffer through virtqueue (must be called on `master-core`)
 * @param queue                 virtqueue to use
 * @param buffer                data buffer to send (must be allocated using esp_amp_queue_alloc_try)
 * @param size                  size of data buffer to send (must not exceed the max queue item size)
 *
 * @retval ESP_OK                   successfully send the data buffer to `remote-core`
 * @retval ESP_ERR_NO_MEM           failed to send, data size too large
 * @retval ESP_ERR_NOT_SUPPORTED    failed to send, expected to be called only on `master-core`
 * @retval ESP_ERR_NOT_ALLOWED      failed to send, send before alloc!
 */
int esp_amp_queue_send_try(esp_amp_queue_t *queue, void* buffer, uint16_t size);

/**
 * Try to receive a data buffer through virtqueue (must be called on `remote-core`)
 * @param queue                 virtqueue to use
 * @param buffer                variable to store the address of the data buffer sent from `master-core`
 * @param size                  size of data buffer received
 *
 * @retval ESP_OK                   successfully receive the data buffer from `master-core`
 * @retval ESP_ERR_NOT_FOUND        no available buffer to receive from `master-core`
 * @retval ESP_ERR_NOT_SUPPORTED    failed to receive, expected to be called only on `remote-core`
 */
int esp_amp_queue_recv_try(esp_amp_queue_t *queue, void** buffer, uint16_t* size);

/**
 * Try to alloc a data buffer which can be filled and sent later (must be called on `master-core`)
 * @param queue                 virtqueue to use
 * @param buffer                variable to store the address of the allocated data buffer
 * @param size                  size of data buffer to allocate
 *
 * @retval ESP_OK                   successfully allocate the data buffer
 * @retval ESP_ERR_NOT_FOUND        no available buffer to allocate
 * @retval ESP_ERR_NO_MEM           too large size of data buffer
 * @retval ESP_ERR_NOT_SUPPORTED    failed to alloc, expected to be called only on `master-core`
 */
int esp_amp_queue_alloc_try(esp_amp_queue_t *queue, void** buffer, uint16_t size);

/**
 * Try to free(give back) the data buffer received from `master-core` (must be called on `remote-core`)
 * @param queue                 virtqueue to use
 * @param buffer                data buffer to free
 *
 * @retval ESP_OK                   successfully free the data buffer
 * @retval ESP_ERR_NOT_SUPPORTED    failed to free, expected to be called only on `remote-core`
 * @retval ESP_ERR_NOT_ALLOWED      failed to free, free before receive!
 */
int esp_amp_queue_free_try(esp_amp_queue_t *queue, void* buffer);

/**
 * Initialize the buffer and descriptor of virtqueue, store the virtqueue config in provided structure
 * @param queue_conf            allocated virtqueue config struct to initialize
 * @param queue_len             virtqueue length
 * @param queue_item_size       maximum item size of virtqueue
 * @param queue_desc            virtqueue descriptor to initialize
 * @param queue_buffer          virtqueue buffer to initialize
 *
 * @retval ESP_OK
 */
int esp_amp_queue_init_buffer(esp_amp_queue_conf_t* queue_conf, uint16_t queue_len, uint16_t queue_item_size, esp_amp_queue_desc_t* queue_desc, void* queue_buffer);

/**
 * Initialize the virtqueue handler based on the virtqueue configuration
 * @param queue                 allocated virtqueue handler to initialize
 * @param queue_conf            initialized virtqueue configuration
 * @param cb_func               callback function, set to `NULL` if not required. When `is_master` is true, it will be invoked after successfully sending data; Otherwise, it will be invoked when receiving new data.
 * @param priv_data             pointer of arbitrary data which will be passed as the argument when invoking cb_func
 * @param is_master             whether to initialize as the role of `master-core` for this virtqueue
 *
 * @retval ESP_OK
 */
int esp_amp_queue_create(esp_amp_queue_t* queue, esp_amp_queue_conf_t* queue_conf, esp_amp_queue_cb_t cb_func, void* priv_data, bool is_master);

#if IS_MAIN_CORE
/**
 * Initialize the virtqueue on main-core
 *
 * @param queue                 allocated virtqueue handler to initialize
 * @param queue_len             the length of `Virtqueue` (number of queue entries), must be power of 2
 * @param queue_item_size       the maximum size of each `Virtqueue` element
 * @param cb_func               callback function, set to `NULL` if not required. When `is_master` is true, it will be invoked after successfully sending data; Otherwise, it will be invoked when receiving new data.
 * @param priv_data             pointer of arbitrary data which will be passed as the argument when invoking cb_func
 * @param is_master             whether to initialize as the role of `master-core` for this virtqueue
 * @param sysinfo_id            sysinfo id of shared memory allocated for virtqueue
 *
 * @retval ESP_OK               successfully initialize the virtqueue
 * @retval ESP_ERR_INVALID_ARG  inappropriate `queue_len`, must be power of 2
 * @retval ESP_ERR_NO_MEM       insufficient shared memory (sysinfo) space
 */
int esp_amp_queue_main_init(esp_amp_queue_t* queue, uint16_t queue_len, uint16_t queue_item_size, esp_amp_queue_cb_t cb_func, void* priv_data, bool is_master, esp_amp_sys_info_id_t sysinfo_id);
#endif

/**
 * Initialize the virtqueue on sub-core
 *
 * @param queue                 allocated virtqueue handler to initialize
 * @param cb_func               callback function, set to `NULL` if not required. When `is_master` is true, it will be invoked after successfully sending data; Otherwise, it will be invoked when receiving new data.
 * @param priv_data             pointer of arbitrary data which will be passed as the argument when invoking cb_func
 * @param is_master             whether to initialize as the role of `master-core` for this virtqueue
 * @param sysinfo_id            sysinfo id of shared memory allocated for virtqueue
 *
 * @retval ESP_OK               successfully initialize the virtqueue
 * @retval ESP_ERR_NOT_FOUND    failed to find corresponding sysinfo entry with given sysinfo_id
 */
int esp_amp_queue_sub_init(esp_amp_queue_t* queue, esp_amp_queue_cb_t cb_func, void* priv_data, bool is_master, esp_amp_sys_info_id_t sysinfo_id);

/**
 * Enable the virtqueue software interrupt handler, must be invoked when handling incoming data with interrupt on `remote-core`
 *
 * @param queue                     virtqueue handler
 * @param sw_intr_id                software interrupt id to be triggered when virtqueue is available
 *
 * @retval ESP_OK                   successfully enable the virtqueue software interrupt handler
 * @retval ESP_ERR_NOT_SUPPORTED    failed to enable, expected to be called only on `remote-core`
 * @retval ESP_ERR_NOT_FINISHED     failed to invoke `esp_amp_sw_intr_add_handler` internally
 */
int esp_amp_queue_intr_enable(esp_amp_queue_t* queue, esp_amp_sw_intr_id_t sw_intr_id);

#define ESP_AMP_QUEUE_AVAILABLE_MASK(bit)                       (uint16_t)((uint16_t)(bit) << 7)
#define ESP_AMP_QUEUE_USED_MASK(bit)                            (uint16_t)((uint16_t)(bit) << 15)
#define ESP_AMP_QUEUE_FLAG_IS_USED(flipCounter, flag)           (((ESP_AMP_QUEUE_AVAILABLE_MASK(1) & (flag)) != ESP_AMP_QUEUE_AVAILABLE_MASK((flipCounter))) && ((ESP_AMP_QUEUE_USED_MASK(1) & (flag)) != ESP_AMP_QUEUE_USED_MASK((flipCounter))))
#define ESP_AMP_QUEUE_FLAG_IS_AVAILABLE(flipCounter, flag)      (((ESP_AMP_QUEUE_AVAILABLE_MASK(1) & (flag)) == ESP_AMP_QUEUE_AVAILABLE_MASK((flipCounter))) && ((ESP_AMP_QUEUE_USED_MASK(1) & (flag)) != ESP_AMP_QUEUE_USED_MASK((flipCounter))))

#ifdef __cplusplus
}
#endif
