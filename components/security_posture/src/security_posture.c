#include "security_posture.h"

#include "esp_efuse.h"
#include "esp_log.h"
#include "esp_secure_boot.h"
#include "esp_system.h"
#include "sdkconfig.h"

static const char *const TAG = "security_posture";

// A cached sdkconfig from an older revision may predate this symbol; the
// fallback keeps incremental builds on the safe development default.
#ifndef CONFIG_SPROUT_PRODUCTION_BUILD
#define CONFIG_SPROUT_PRODUCTION_BUILD 0
#endif

static bool security_posture_is_production_build(void) {
#if CONFIG_SPROUT_PRODUCTION_BUILD
    return true;
#else
    return false;
#endif
}

security_posture_features_t security_posture_read_features(void) {
    const security_posture_features_t features = {
        .secure_boot_enabled = esp_secure_boot_enabled(),
        .flash_encryption_enabled = esp_efuse_is_flash_encryption_enabled(),
    };
    return features;
}

esp_err_t security_posture_enforce(void) {
    const security_posture_features_t features =
        security_posture_read_features();
    const security_posture_verdict_t verdict = security_posture_evaluate(
        security_posture_is_production_build(),
        &features
    );
    switch (verdict) {
        case SECURITY_POSTURE_VERDICT_OK:
            return ESP_OK;
        case SECURITY_POSTURE_VERDICT_INSECURE_BOOT:
            ESP_LOGE(
                TAG,
                "refusing unhardened production image: secure_boot_disabled"
            );
            return ESP_ERR_INVALID_STATE;
        case SECURITY_POSTURE_VERDICT_UNENCRYPTED_FLASH:
            ESP_LOGE(
                TAG,
                "refusing unhardened production image: "
                "flash_encryption_disabled"
            );
            return ESP_ERR_INVALID_STATE;
        default:
            ESP_LOGE(TAG, "refusing unhardened production image: unknown");
            return ESP_ERR_INVALID_STATE;
    }
}

void security_posture_halt(void) {
    for (;;) {
        // esp_restart() never returns, so the guard cannot be optimized into
        // a fall-through. The device keeps resetting and the bootloader keeps
        // refusing the image long before any application module starts.
        esp_restart();
    }
}

esp_err_t security_posture_guard(void) {
    if (security_posture_enforce() != ESP_OK) {
        security_posture_halt();
    }
    return ESP_OK;
}

const module_descriptor_t *security_posture_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "security_posture",
        .version = "1.0.0",
        .initialize = security_posture_guard,
        .shutdown = NULL,
    };
    return &descriptor;
}
