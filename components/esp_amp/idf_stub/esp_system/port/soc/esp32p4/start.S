/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"

    .section .text
    .type   _start, @function
    .global _start
_start:
    /* Setup the global pointer, which the ABI assumes points to the
     * __global_pointer$ symbol. */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

   /* Clear the bss segment */
    la      a0, _edata
    la      a2, _end
    sub     a2, a2, a0
    li      a1, 0
    call    memset

    /* Set up the stack pointer */
    la      sp, __stack_top

    la      a0, __libc_fini_array   # Register global termination functions
    call    atexit                  #  to be called upon exit
    call    __libc_init_array       # Run global initialization functions

    /* Call main */
    li      a0, 0
    call    subcore_startup
    tail    exit

    .size  _start, .-_start

    /* These don't have to do anything since we use init_array/fini_array. */
    .global _init
    .type   _init, @function
    .global _fini
    .type   _fini, @function
_init:
_fini:
    ret

    .size  _init, .-_init
    .size  _fini, .-_fini
