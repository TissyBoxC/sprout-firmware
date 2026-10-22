#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Microseconds represented by one FreeRTOS tick in the host build. */
#define VOICE_INTERCOM_HOST_TICK_US 1000u

#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS 1
#endif

typedef uint32_t TickType_t;
typedef void *TaskHandle_t;

#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)
#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Advance the simulated tick counter used as the monotonic clock. */
void voice_intercom_host_advance_ticks(uint32_t ticks);

#ifdef __cplusplus
}
#endif
