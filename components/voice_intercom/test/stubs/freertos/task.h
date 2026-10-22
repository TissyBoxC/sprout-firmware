#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Record the created task instead of scheduling it on the host. */
typedef void (*TaskEntry_t)(void *argument);

int xTaskCreate(
    TaskEntry_t task,
    const char *name,
    uint32_t stack_size,
    void *argument,
    unsigned int priority,
    TaskHandle_t *task_handle
);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
TickType_t xTaskGetTickCount(void);

#ifdef __cplusplus
}
#endif
