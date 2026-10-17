#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *argument);

#ifdef __cplusplus
extern "C" {
#endif

BaseType_t xTaskCreate(
    TaskFunction_t task,
    const char *name,
    uint32_t stack_size,
    void *argument,
    UBaseType_t priority,
    TaskHandle_t *task_handle
);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);

#ifdef __cplusplus
}
#endif
