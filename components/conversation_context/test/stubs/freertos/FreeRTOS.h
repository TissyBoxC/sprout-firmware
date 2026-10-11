#pragma once

// Minimal single-threaded host stand-in for the FreeRTOS primitives the
// conversation-context runtime uses for mutual exclusion. The host test runs
// on one thread, so lock/unlock only need to report success.

#include <stddef.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void *SemaphoreHandle_t;

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY ((UBaseType_t)0xffffffffu)
