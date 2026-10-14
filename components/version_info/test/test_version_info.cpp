#include "version_info.h"

#include "esp_app_desc.h"

#include <cassert>
#include <cstring>

namespace {

void test_versions_are_reported() {
    version_info_test_set_firmware_version("0.23.0");
    const version_info_t info = version_info_get();

    assert(std::strcmp(info.schema_version, "1.0.0") == 0);
    assert(std::strcmp(info.firmware_version, "0.23.0") == 0);
    assert(std::strcmp(info.hardware_revision, CONFIG_IDF_TARGET) == 0);
    assert(std::strcmp(info.protocol_version, "1.0.0") == 0);
}

void test_initialize_rejects_empty_firmware_version() {
    const module_descriptor_t *descriptor = version_info_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize != nullptr);

    version_info_test_set_firmware_version("0.23.0");
    assert(descriptor->initialize() == ESP_OK);

    // A missing app version means the image metadata is unusable.
    version_info_test_set_firmware_version("");
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor = version_info_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "version_info") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->shutdown == nullptr);
}

}  // namespace

int main() {
    test_versions_are_reported();
    test_initialize_rejects_empty_firmware_version();
    test_module_descriptor_is_stable();
    return 0;
}
