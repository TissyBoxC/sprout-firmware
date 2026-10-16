#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Minimal host stand-in for the ESP-IDF default event loop. The handle is an
// opaque pointer the test only checks for non-null / stability.
typedef struct esp_event_loop_instance *esp_event_loop_handle_t;

#define tskNO_AFFINITY ((uint32_t)0x7FFFFFFF)

typedef struct {
    uint32_t queue_size;
    const char *task_name;
    uint32_t task_priority;
    uint32_t task_stack_size;
    uint32_t task_core_id;
} esp_event_loop_args_t;

esp_err_t esp_event_loop_create(
    const esp_event_loop_args_t *args,
    esp_event_loop_handle_t *handle_out
);
