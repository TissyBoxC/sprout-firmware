#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

#define pdPASS 1
#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))

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
