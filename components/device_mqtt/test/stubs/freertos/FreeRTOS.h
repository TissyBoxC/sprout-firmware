#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
typedef void *QueueHandle_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))
