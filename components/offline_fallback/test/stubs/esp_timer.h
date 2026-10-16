#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct esp_timer *esp_timer_handle_t;

typedef void (*esp_timer_cb_t)(void *argument);

typedef struct {
    esp_timer_cb_t callback;
    const char *name;
} esp_timer_create_args_t;

esp_err_t esp_timer_create(
    const esp_timer_create_args_t *args,
    esp_timer_handle_t *handle_out
);
esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t handle);
esp_err_t esp_timer_delete(esp_timer_handle_t handle);
