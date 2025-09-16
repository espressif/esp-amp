/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <cmath>
#include <stdlib.h>
#include <string.h>

#include "esp_amp.h"

#define SYS_INFO_ID_TEST_BITS 0x0000
#define SW_INTR_ID_ISR_FLOAT  SW_INTR_ID_0
#define EVENT_SUBCORE_READY   (1 << 0)

#define TEST_MATH (1 << 0)
#define TEST_CPP_INIT_ARRAY (1 << 1)
#define TEST_MEMORY (1 << 2)
#define TEST_STRING (1 << 3)
#define TEST_FLOAT_IN_ISR (1 << 4)
#define TEST_ALL (TEST_MATH | TEST_CPP_INIT_ARRAY | TEST_MEMORY | TEST_STRING | TEST_FLOAT_IN_ISR)

// Flag to check if the constructor is called
atomic_uint *test_bits = NULL;
bool test_cpp_init_array_called = false;

// A global object with a constructor
class GlobalObject {
public:
    GlobalObject()
    {
        // Set the flag when the constructor is executed
        test_cpp_init_array_called = true;
        printf("GlobalObject constructor called\r\n");
    }
};

// Instantiate the global object
GlobalObject global_instance;

bool areAlmostEqual(double a, double b)
{
    return fabs(a - b) < 1e-6;
}

void test_math()
{
    volatile float input_a = 3.0 * M_PI / 2.0;
    volatile float input_b = 0.0;
    volatile float input_c = 1.0;
    volatile float input_d = 2.0;
    volatile float input_e = 4.0;
    volatile float input_f = 1.5;

    assert(areAlmostEqual(sin(input_a), -1));       // sin(3pi/2) = -1
    assert(areAlmostEqual(cos(input_a), 0));        // cos(3pi/2) = 0
    assert(areAlmostEqual(tan(input_a / 2.0), -1)); // tan(3pi/4) = -1

    assert(log(input_c) == 0);                      // log(1) = 0
    assert(log10(input_c) == 0);                    // log10(1) = 0
    assert(exp(input_b) == 1);                      // exp(0) = 1
    assert(sqrt(input_e) == 2);                     // sqrt(4) = 2
    assert(pow(input_d, 3) == 8);                   // 2^3 = 8

    assert(areAlmostEqual(fabs(-input_f), 1.5));    // abs(-1.5) = 1.5
    assert(ceil(input_f) == 2);                     // ceil(1.5) = 2
    assert(floor(input_f) == 1);                    // floor(1.5) = 1
    assert(round(input_f) == 2);                    // round(1.5) = 2
    assert(areAlmostEqual(fmod(input_f, 3), 1.5));  // fmod(1.5, 3) = 1.5

    atomic_fetch_or(test_bits, TEST_MATH);
}

void test_string()
{
    // Test strcmp
    assert(strcmp("hello", "hello") == 0);
    assert(strcmp("hello", "world") < 0);
    assert(strcmp("world", "hello") > 0);

    // Test strlen
    assert(strlen("test") == 4);

    // Test strcpy
    char buffer[20];
    strcpy(buffer, "test");
    assert(strcmp(buffer, "test") == 0);

    atomic_fetch_or(test_bits, TEST_STRING);
}

#if CONFIG_ESP_AMP_SUBCORE_ENABLE_HEAP
void test_basic_allocation()
{
    for (int i = 1; i <= 10; i++) {
        void *ptr = malloc(i * 16 * sizeof(int));
        assert(ptr != NULL);
        free(ptr);
    }
}

void test_zero_allocation()
{
    void *ptr = malloc(0);
    free(ptr);

    ptr = calloc(0, 10);
    free(ptr);
}

void test_large_allocation()
{
    void *ptr = malloc(INT32_MAX / 2);
    assert(ptr == NULL);
    free(ptr);
}

void test_double_free()
{
    void *ptr = malloc(100);
    assert(ptr != NULL);
    free(ptr);
    free(ptr);
}

void test_realloc()
{
    void *ptr = malloc(100);
    assert(ptr != NULL);

    // Shrink memory
    void *new_ptr = realloc(ptr, 50);
    assert(new_ptr != NULL);

    // Expand memory
    new_ptr = realloc(new_ptr, 200);
    assert(new_ptr != NULL);

    // Free memory
    new_ptr = realloc(new_ptr, 0);
    assert(new_ptr == NULL);
}

void test_calloc_initialization()
{
    size_t num_elements = 10;
    size_t element_size = sizeof(int);

    int *arr = (int *)calloc(num_elements, element_size);
    assert(arr != NULL);

    for (size_t i = 0; i < num_elements; i++) {
        assert(arr[i] == 0);
    }

    free(arr);
}
#endif /* CONFIG_ESP_AMP_SUBCORE_ENABLE_HEAP */

void test_memory()
{

    // Test malloc
#if CONFIG_ESP_AMP_SUBCORE_ENABLE_HEAP
    test_basic_allocation();
    test_zero_allocation();
    test_large_allocation();
    test_double_free();
    test_realloc();
    test_calloc_initialization();
#else
    void *ptr = malloc(100);
    assert(ptr == NULL);
#endif

    // Test memset
    char buffer[10];
    memset(buffer, 'A', sizeof(buffer));
    for (int i = 0; i < 10; i++) {
        assert(buffer[i] == 'A');
    }

    // Test memcpy
    char src[] = "Hello, World!";
    char dest[20];
    memcpy(dest, src, strlen(src) + 1);
    assert(strcmp(dest, src) == 0);

    // Test memmove
    char src2[] = "Hello, World!";
    char dest2[20];
    memmove(dest2, src2, strlen(src2) + 1);
    assert(strcmp(dest2, src2) == 0);

    // Test memcmp
    char str1[] = "Hello";
    char str2[] = "World";
    assert(memcmp(str1, str2, 5) < 0);
    assert(memcmp(str2, str1, 5) > 0);
    assert(memcmp(str1, str1, 5) == 0);

    atomic_fetch_or(test_bits, TEST_MEMORY);
}

int test_float_in_isr(void *args)
{
    printf("ISR float test\r\n");
    volatile float a = 3.14159;
    volatile float b = 2.71828;
    float c = a + b;
    printf("ISR float test result: %f\r\n", c);
    atomic_fetch_or(test_bits, TEST_FLOAT_IN_ISR);
    return 0;
}

extern "C" int main(void)
{
    esp_amp_init();

    test_bits = (atomic_uint *)esp_amp_sys_info_get(SYS_INFO_ID_TEST_BITS, NULL, SYS_INFO_CAP_HP);

    esp_amp_sw_intr_add_handler(SW_INTR_ID_ISR_FLOAT, test_float_in_isr, NULL);

    esp_amp_event_notify(EVENT_SUBCORE_READY);

    test_math();
    test_string();
    test_memory();

    if (test_cpp_init_array_called) {
        atomic_fetch_or(test_bits, TEST_CPP_INIT_ARRAY);
    }

    while (1) {
        esp_amp_platform_delay_ms(1000);
    }
}
