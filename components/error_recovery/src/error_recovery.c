#include "error_recovery.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *const TAG = "error_recovery";
static portMUX_TYPE error_recovery_lock = portMUX_INITIALIZER_UNLOCKED;
static error_recovery_snapshot_t last_snapshot;
static error_recovery_recovery_t last_recovery;
static error_recovery_recovery_t
    recovery_events[ERROR_RECOVERY_RECOVERY_EVENT_CAPACITY];
static size_t recovery_event_count;
static unsigned int transition_count;

static void error_recovery_copy_text(
    char *output,
    size_t output_size,
    const char *input
) {
    if (output == NULL || output_size == 0) {
        return;
    }
    output[0] = '\0';
    if (input == NULL) {
        return;
    }

    size_t output_index = 0;
    while (input[output_index] != '\0' && output_index + 1 < output_size) {
        const unsigned char value = (unsigned char)input[output_index];
        output[output_index] =
            value >= 0x20 && value <= 0x7e ? (char)value : '_';
        ++output_index;
    }
    output[output_index] = '\0';
}

static void error_recovery_append_recovery_locked(
    const error_recovery_recovery_t *event
) {
    if (recovery_event_count == ERROR_RECOVERY_RECOVERY_EVENT_CAPACITY) {
        memmove(
            &recovery_events[0],
            &recovery_events[1],
            sizeof(recovery_events[0]) *
                (ERROR_RECOVERY_RECOVERY_EVENT_CAPACITY - 1)
        );
        --recovery_event_count;
    }
    recovery_events[recovery_event_count++] = *event;
}

static void error_recovery_module_failed(
    const char *module_name,
    esp_err_t error_code
) {
    (void)error_recovery_record(module_name, error_code);
}

static void error_recovery_module_recovered(const char *module_name) {
    (void)error_recovery_mark_recovered(module_name);
}

esp_err_t error_recovery_record(const char *module_name, esp_err_t error_code) {
    if (module_name == NULL || error_code == ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    char copied_name[ERROR_RECOVERY_MODULE_NAME_SIZE] = {0};
    error_recovery_copy_text(
        copied_name,
        sizeof(copied_name),
        module_name
    );
    if (copied_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&error_recovery_lock);
    if (transition_count != UINT_MAX) {
        ++transition_count;
    }
    memcpy(
        last_snapshot.module_name,
        copied_name,
        sizeof(last_snapshot.module_name)
    );
    last_snapshot.error_code = error_code;
    ++last_snapshot.failure_count;
    last_snapshot.transition_count = transition_count;
    // A failure closes the previous recovery cycle so the next successful
    // initialization produces a distinct diagnostic transition.
    memset(&last_recovery, 0, sizeof(last_recovery));
    portEXIT_CRITICAL(&error_recovery_lock);

    ESP_LOGW(TAG, "%s reported %s", copied_name, esp_err_to_name(error_code));
    return ESP_OK;
}

esp_err_t error_recovery_mark_recovered(const char *module_name) {
    if (module_name == NULL || module_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char copied_name[ERROR_RECOVERY_MODULE_NAME_SIZE] = {0};
    error_recovery_copy_text(
        copied_name,
        sizeof(copied_name),
        module_name
    );
    if (copied_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    bool duplicate = false;
    portENTER_CRITICAL(&error_recovery_lock);
    if (last_recovery.module_name[0] != '\0' &&
        strcmp(last_recovery.module_name, copied_name) == 0) {
        duplicate = true;
    } else {
        error_recovery_recovery_t event = {};
        if (transition_count != UINT_MAX) {
            ++transition_count;
        }
        memcpy(event.module_name, copied_name, sizeof(event.module_name));
        event.transition_count = transition_count;
        error_recovery_append_recovery_locked(&event);
        last_recovery = event;
    }
    portEXIT_CRITICAL(&error_recovery_lock);

    if (!duplicate) {
        ESP_LOGI(TAG, "%s recovered", copied_name);
    }
    return ESP_OK;
}

void error_recovery_restart(const char *module_name, esp_err_t error_code) {
    (void)error_recovery_record(module_name, error_code);
    ESP_LOGE(TAG, "restarting after %s", module_name);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_ERROR_RECOVERY_RESTART_DELAY_MS));
    esp_restart();
}

error_recovery_snapshot_t error_recovery_get_snapshot(void) {
    error_recovery_snapshot_t snapshot = {};
    portENTER_CRITICAL(&error_recovery_lock);
    snapshot = last_snapshot;
    portEXIT_CRITICAL(&error_recovery_lock);
    return snapshot;
}

error_recovery_recovery_snapshot_t error_recovery_get_recoveries(void) {
    error_recovery_recovery_snapshot_t snapshot = {};
    portENTER_CRITICAL(&error_recovery_lock);
    snapshot.latest_transition_count = transition_count;
    snapshot.event_count = recovery_event_count;
    for (size_t index = 0; index < recovery_event_count; ++index) {
        snapshot.events[index] = recovery_events[index];
    }
    portEXIT_CRITICAL(&error_recovery_lock);
    return snapshot;
}

static esp_err_t error_recovery_initialize(void) {
    module_registry_set_failure_handler(error_recovery_module_failed);
    module_registry_set_recovery_handler(error_recovery_module_recovered);
    return ESP_OK;
}

const module_descriptor_t *error_recovery_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "error_recovery",
        .version = "1.0.0",
        .initialize = error_recovery_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
