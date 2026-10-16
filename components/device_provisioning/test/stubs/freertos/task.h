#pragma once

#include <stddef.h>

#include "freertos/FreeRTOS.h"

typedef void (*TaskFunction_t)(void *argument);

#ifdef __cplusplus
extern "C" {
#endif

BaseType_t xTaskCreate(
    TaskFunction_t task,
    const char *name,
    uint32_t stack_size,
    void *argument,
    unsigned int priority,
    void *task_handle
);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(void *task);

#ifdef __cplusplus
}
#endif
