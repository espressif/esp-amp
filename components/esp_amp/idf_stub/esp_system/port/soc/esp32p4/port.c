/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Aborts execution when a coprocessor was used in an ISR context
 */
void vPortCoprocUsedInISR(void* frame)
{
    extern void xt_unhandled_exception(void* frame);
    xt_unhandled_exception(frame);
}
