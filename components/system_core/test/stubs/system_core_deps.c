#include "esp_event.h"

// A single controllable stand-in loop. The test uses the injection hook to
// force a creation failure in its own process.

static int host_loop_storage;
static int host_create_calls;
static esp_err_t host_create_result = ESP_OK;

esp_err_t esp_event_loop_create(
    const esp_event_loop_args_t *args,
    esp_event_loop_handle_t *handle_out
) {
    ++host_create_calls;
    if (host_create_result != ESP_OK) {
        return host_create_result;
    }
    if (args == NULL || handle_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // The default loop owns its queue and task; the host only needs a stable
    // non-null handle, so a single static instance is enough.
    *handle_out = (esp_event_loop_handle_t)&host_loop_storage;
    return ESP_OK;
}

void system_core_host_set_create_result(esp_err_t result) {
    host_create_result = result;
}

int system_core_host_create_calls(void) {
    return host_create_calls;
}
