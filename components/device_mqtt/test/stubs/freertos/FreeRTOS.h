#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef struct {
    int placeholder;
} StaticSemaphore_t;

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t)0xffffffffu)
#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))
