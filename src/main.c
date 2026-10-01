#include "esp_err.h"
#include "esp_log.h"

#if CONFIG_FEATURE_ERROR_CODE
#include "error_code.h"
#endif
#if CONFIG_FEATURE_DEVICE_IDENTITY
#include "device_identity.h"
#endif
#if CONFIG_FEATURE_ERROR_RECOVERY
#include "error_recovery.h"
#endif
#if CONFIG_FEATURE_MODULE_REGISTRY
#include "module_registry.h"
#endif
#if CONFIG_FEATURE_SYSTEM_CORE
#include "system_core.h"
#endif
#if CONFIG_FEATURE_UI_TEXT
#include "ui_text.h"
#endif
#if CONFIG_FEATURE_VERSION_INFO
#include "version_info.h"
#endif

static const char *const TAG = "sprout_main";

#if CONFIG_FEATURE_MODULE_REGISTRY
static void register_modules(void) {
#if CONFIG_FEATURE_SYSTEM_CORE
    ESP_ERROR_CHECK(module_registry_add(system_core_module_descriptor()));
#endif
#if CONFIG_FEATURE_VERSION_INFO
    ESP_ERROR_CHECK(module_registry_add(version_info_module_descriptor()));
#endif
#if CONFIG_FEATURE_ERROR_CODE
    ESP_ERROR_CHECK(module_registry_add(error_code_module_descriptor()));
#endif
#if CONFIG_FEATURE_DEVICE_IDENTITY
    ESP_ERROR_CHECK(module_registry_add(device_identity_module_descriptor()));
#endif
#if CONFIG_FEATURE_ERROR_RECOVERY
    ESP_ERROR_CHECK(module_registry_add(error_recovery_module_descriptor()));
#endif
#if CONFIG_FEATURE_UI_TEXT
    ESP_ERROR_CHECK(module_registry_add(ui_text_module_descriptor()));
#endif
}
#endif

void app_main(void) {
#if CONFIG_FEATURE_MODULE_REGISTRY
    register_modules();

    const esp_err_t result = module_registry_initialize_all();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "one or more modules failed to initialize: %s",
                 esp_err_to_name(result));
    }
#elif CONFIG_FEATURE_SYSTEM_CORE
    ESP_ERROR_CHECK(system_core_init());
#endif
}
