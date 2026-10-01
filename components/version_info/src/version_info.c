#include "version_info.h"

#include "esp_app_desc.h"

version_info_t version_info_get(void) {
    const esp_app_desc_t *app_description = esp_app_get_description();
    const version_info_t version_information = {
        .schema_version = CONFIG_FIRMWARE_SCHEMA_VERSION,
        .firmware_version = app_description->version,
        .hardware_revision = CONFIG_IDF_TARGET,
        .protocol_version = CONFIG_FIRMWARE_PROTOCOL_VERSION,
    };
    return version_information;
}

static esp_err_t version_info_initialize(void) {
    return version_info_get().firmware_version[0] == '\0'
               ? ESP_ERR_INVALID_STATE
               : ESP_OK;
}

const module_descriptor_t *version_info_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "version_info",
        .version = "1.0.0",
        .initialize = version_info_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
