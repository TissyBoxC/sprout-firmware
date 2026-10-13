#pragma once

// Minimal single-threaded host stand-in for the FreeRTOS mutex primitives the
// local command runtime uses. The host test runs on one thread, so a non-NULL
// handle is enough to model lock/unlock success without real synchronisation.

#include "freertos/FreeRTOS.h"

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    static int host_mutex;
    return &host_mutex;
}

static inline BaseType_t xSemaphoreTake(
    SemaphoreHandle_t semaphore,
    UBaseType_t timeout
) {
    (void)semaphore;
    (void)timeout;
    return pdTRUE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    return pdTRUE;
}
