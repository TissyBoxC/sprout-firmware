#include "ota_rollback.h"

#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "module_registry.h"
#include "ota_rollback_policy.h"

#ifndef CONFIG_OTA_ROLLBACK_MAX_BOOT_ATTEMPTS
#define CONFIG_OTA_ROLLBACK_MAX_BOOT_ATTEMPTS 3
#endif

static const char *const TAG = "ota_rollback";

static bool ota_rollback_ready;
static bool ota_rollback_pending_verify;
static uint32_t ota_rollback_boot_count;
#define OTA_ROLLBACK_BOOT_COUNT_KEY "ota_boot_count"

static uint32_t ota_rollback_read_boot_count(void) {
    char value[16] = {0};
    if (config_store_get_string(
            OTA_ROLLBACK_BOOT_COUNT_KEY,
            value,
            sizeof(value)
        ) != ESP_OK) {
        return 0;
    }
    unsigned long parsed = 0;
    if (sscanf(value, "%lu", &parsed) != 1 || parsed > UINT32_MAX) {
        return 0;
    }
    return (uint32_t)parsed;
}

static esp_err_t ota_rollback_write_boot_count(uint32_t value) {
    char encoded[16] = {0};
    snprintf(encoded, sizeof(encoded), "%lu", (unsigned long)value);
    return config_store_set_string(OTA_ROLLBACK_BOOT_COUNT_KEY, encoded);
}

esp_err_t ota_rollback_init(void) {
    if (ota_rollback_ready) {
        return ESP_OK;
    }
    const esp_partition_t *running_partition =
        esp_ota_get_running_partition();
    if (running_partition == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t state_result = esp_ota_get_state_partition(
        running_partition,
        &image_state
    );
    if (state_result == ESP_ERR_NOT_FOUND ||
        state_result == ESP_ERR_NOT_SUPPORTED) {
        ota_rollback_pending_verify = false;
    } else if (state_result != ESP_OK) {
        return state_result;
    } else {
        ota_rollback_pending_verify =
            image_state == ESP_OTA_IMG_PENDING_VERIFY;
    }
    if (ota_rollback_pending_verify) {
        ota_rollback_boot_count = ota_rollback_read_boot_count();
        if (ota_rollback_boot_count < UINT32_MAX) {
            ++ota_rollback_boot_count;
        }
        const esp_err_t count_result = ota_rollback_write_boot_count(
            ota_rollback_boot_count
        );
        if (count_result != ESP_OK) {
            return count_result;
        }
    } else {
        ota_rollback_boot_count = 0;
        (void)config_store_erase_key(OTA_ROLLBACK_BOOT_COUNT_KEY);
    }
    ota_rollback_ready = true;
    return ESP_OK;
}

bool ota_rollback_is_pending_verify(void) {
    return ota_rollback_pending_verify;
}

uint32_t ota_rollback_get_boot_count(void) {
    return ota_rollback_boot_count;
}

esp_err_t ota_rollback_mark_valid(void) {
    if (!ota_rollback_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result == ESP_OK) {
        ota_rollback_pending_verify = false;
        ota_rollback_boot_count = 0;
        (void)config_store_erase_key(OTA_ROLLBACK_BOOT_COUNT_KEY);
    }
    return result;
}

esp_err_t ota_rollback_reboot_if_unhealthy(void) {
    if (!ota_rollback_ready || !ota_rollback_pending_verify) {
        return ESP_OK;
    }
    if (!ota_rollback_boot_count_exceeded(
            ota_rollback_boot_count,
            CONFIG_OTA_ROLLBACK_MAX_BOOT_ATTEMPTS
        )) {
        return ESP_OK;
    }
    if (!esp_ota_check_rollback_is_possible()) {
        ESP_LOGE(TAG, "pending image is unhealthy but no rollback slot exists");
        return ESP_ERR_OTA_ROLLBACK_FAILED;
    }
    ESP_LOGW(TAG, "pending image exceeded boot budget; rolling back");
    return esp_ota_mark_app_invalid_rollback_and_reboot();
}

const module_descriptor_t *ota_rollback_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "ota_rollback",
        .version = "1.0.0",
        .initialize = ota_rollback_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
