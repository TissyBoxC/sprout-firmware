#include "system_core.h"

#include <stdbool.h>

#include "esp_event.h"

static bool system_core_is_initialized;
static esp_event_loop_handle_t system_core_loop;

esp_err_t system_core_init(void) {
    if (system_core_is_initialized) {
        return ESP_OK;
    }

    const esp_event_loop_args_t loop_args = {
        .queue_size = 16,
        .task_name = "sprout_events",
        .task_priority = 5,
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY,
    };

    esp_err_t result = esp_event_loop_create(&loop_args, &system_core_loop);
    if (result != ESP_OK) {
        return result;
    }

    system_core_is_initialized = true;
    return ESP_OK;
}

esp_event_loop_handle_t system_core_event_loop(void) {
    return system_core_loop;
}

const module_descriptor_t *system_core_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "system_core",
        .version = "1.0.0",
        .initialize = system_core_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
