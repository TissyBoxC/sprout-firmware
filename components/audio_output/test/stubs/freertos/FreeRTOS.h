#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
