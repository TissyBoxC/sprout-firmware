#include "ota_validate.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_log.h"

static const char *const TAG = "ota_validate";

esp_err_t ota_validate_partition(
    const esp_partition_t *partition,
    const ota_manifest_t *manifest
) {
    if (partition == NULL || manifest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (partition->type != ESP_PARTITION_TYPE_APP) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_app_desc_t application_description = {0};
    esp_err_t result = esp_ota_get_partition_description(
        partition,
        &application_description
    );
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "partition descriptor rejected: %s",
                 esp_err_to_name(result));
        return result;
    }
    if (strcmp(
            application_description.version,
            manifest->firmware_version
        ) != 0) {
        ESP_LOGE(TAG, "partition version does not match manifest");
        return ESP_ERR_INVALID_VERSION;
    }

    // esp_ota_set_boot_partition performs the full image verification,
    // including Secure Boot when enabled. Keep the explicit verification here
    // before changing boot selection so a corrupt write is never selected.
    const esp_partition_pos_t partition_position = {
        .offset = partition->address,
        .size = partition->size,
    };
    esp_image_metadata_t image_metadata = {0};
    result = esp_image_verify(
        ESP_IMAGE_VERIFY,
        &partition_position,
        &image_metadata
    );
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "image verification failed: %s",
                 esp_err_to_name(result));
        return result;
    }

#ifdef CONFIG_SECURE_SIGNED_APPS
    if (!manifest->signature[0] || !manifest->signature_key_id[0]) {
        // Secure Boot builds must never accept an unsigned manifest.
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
#else
    // Even without Secure Boot hardware, require signed release metadata so a
    // manifest cannot be silently downgraded to an unsigned path.
    if (!manifest->signature[0] || !manifest->signature_key_id[0]) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
#endif
    return ESP_OK;
}
