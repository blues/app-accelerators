// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#pragma once

// Init function
bool _init(void);

// Mutex functions
void _lock_wire(void);
void _unlock_wire(void);
void _lock_note(void);
void _unlock_note(void);

// Basic allocation functions
void *_malloc(size_t size);
void _free(void *);

// Time functions
void _delay(uint32_t ms);
uint32_t _millis(void);

// Include the implementation
#ifdef APP_MAIN

#if defined(INC_FREERTOS_H)

SemaphoreHandle_t _wireMutex;
SemaphoreHandle_t _noteMutex;
bool _init()
{
    _wireMutex = xSemaphoreCreateMutex();
    if (_wireMutex == NULL) {
        return false;
    }
    _noteMutex = xSemaphoreCreateMutex();
    if (_noteMutex == NULL) {
        return false;
    }
    return true;
}
void _lock_wire()
{
    xSemaphoreTake(_wireMutex, portMAX_DELAY);
}
void _unlock_wire()
{
    xSemaphoreGive(_wireMutex);
}
void _lock_note()
{
    xSemaphoreTake(_noteMutex, portMAX_DELAY);
}
void _unlock_note()
{
    xSemaphoreGive(_noteMutex);
}
void *_malloc(size_t size)
{
    return pvPortMalloc(size);
}
void _free(void *p)
{
    vPortFree(p);
}
void _delay(uint32_t ms)
{
    vTaskDelay((uint32_t)((((uint64_t) ms * configTICK_RATE_HZ)) / 1000LL));
}
uint32_t _millis(void)
{
    return (uint32_t) ((((uint64_t) xTaskGetTickCount()) * 1000LL) / configTICK_RATE_HZ);
}

#elif defined(ARDUINO)

void _lock_wire() {}
void _unlock_wire() {}
void _lock_note() {}
void _unlock_note() {}
bool _init()
{
    return true;
}
void *_malloc(size_t size)
{
    return malloc(size);
}
void _free(void *p)
{
    free(p);
}
void _delay(uint32_t ms)
{
    delay(ms);
}
uint32_t _millis(void)
{
    return millis();
}

#else

__attribute__((weak)) void _lock_wire() {}
__attribute__((weak)) void _unlock_wire() {}
__attribute__((weak)) void _lock_note() {}
__attribute__((weak)) void _unlock_note() {}
void *_malloc(size_t size)
{
    return malloc(size);
}
void _free(void *p)
{
    free(p);
}
__attribute__((weak)) void _delay(uint32_t ms) {}
__attribute__((weak)) uint32_t _millis(void)
{
    return 0;
}

#endif	// Which RTOS

#endif	// APP_MAIN
