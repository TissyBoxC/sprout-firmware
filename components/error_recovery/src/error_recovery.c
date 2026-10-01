#include "error_recovery.h"

#include <stddef.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *const TAG = "error_recovery";
static error_recovery_snapshot_t last_snapshot;

esp_err_t error_recovery_record(const char *module_name, esp_err_t error_code) {
    if (module_name == NULL || error_code == ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    last_snapshot.module_name = module_name;
    last_snapshot.error_code = error_code;
    ++last_snapshot.failure_count;

    ESP_LOGW(TAG, "%s reported %s", module_name, esp_err_to_name(error_code));
    return ESP_OK;
}

void error_recovery_restart(const char *module_name, esp_err_t error_code) {
    (void)error_recovery_record(module_name, error_code);
    ESP_LOGE(TAG, "restarting after %s", module_name);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_ERROR_RECOVERY_RESTART_DELAY_MS));
    esp_restart();
}

error_recovery_snapshot_t error_recovery_get_snapshot(void) {
    return last_snapshot;
}

static esp_err_t error_recovery_initialize(void) {
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
