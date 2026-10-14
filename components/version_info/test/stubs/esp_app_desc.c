#include "esp_app_desc.h"

#include <string.h>

static esp_app_desc_t version_info_test_app_description = {
    .magic_word = 0xABCD5432u,
    .secure_version = 0u,
    .version = "0.23.0",
    .project_name = "sprout",
    .idf_ver = "v5.5",
};

const esp_app_desc_t *esp_app_get_description(void) {
    return &version_info_test_app_description;
}

void version_info_test_set_firmware_version(const char *version) {
    strncpy(
        version_info_test_app_description.version,
        version,
        sizeof(version_info_test_app_description.version) - 1
    );
    version_info_test_app_description.version[
        sizeof(version_info_test_app_description.version) - 1
    ] = '\0';
}
