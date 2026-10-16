#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef int BaseType_t;
typedef unsigned int TickType_t;

#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY ((TickType_t)-1)
#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))

#ifdef __cplusplus
extern "C" {
#endif

/* FreeRTOS types are plain C types. */

#ifdef __cplusplus
}
#endif
