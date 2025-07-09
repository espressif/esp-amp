/*
* SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
*
* SPDX-License-Identifier: Apache-2.0
*/

#include "sdkconfig.h"
#include "stdio.h"

#if CONFIG_ESP_AMP_SUBCORE_ENABLE_HEAP
extern char _end;
extern char __heap_end;
static char *heap_ptr = &_end;
#endif

struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}

void _fstat_r(void) {}

void _close_r(void) {}

void _lseek_r(void) {}

void _read_r(void) {}

void _write_r(void) {}

void _getpid_r(void) {}

void _kill_r(void) {}

#if CONFIG_ESP_AMP_SUBCORE_ENABLE_HEAP
void* _sbrk(int increment)
{
    char *prev_heap_ptr = heap_ptr;
    if ((heap_ptr + increment) > &__heap_end) {
        printf("Heap out of memory\r\n");
        return (void*) -1;
    }

    heap_ptr += increment;
    return (void*)prev_heap_ptr;
}
#else
void* _sbrk(int increment)
{
    return (void *) -1;
}
#endif

void esp_amp_include_syscalls_impl(void)
{
}
