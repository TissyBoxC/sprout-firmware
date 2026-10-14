#include "error_code.h"

#include <cassert>
#include <cstring>

namespace {

void test_contract_strings_are_stable() {
    assert(std::strcmp(error_code_to_string(ERROR_CODE_OK), "ok") == 0);
    assert(std::strcmp(
               error_code_to_string(ERROR_CODE_INVALID_REQUEST),
               "invalid_request"
           ) == 0);
    assert(std::strcmp(
               error_code_to_string(ERROR_CODE_UNAUTHENTICATED),
               "unauthenticated"
           ) == 0);
    assert(std::strcmp(
               error_code_to_string(ERROR_CODE_PERMISSION_DENIED),
               "permission_denied"
           ) == 0);
    assert(std::strcmp(
               error_code_to_string(ERROR_CODE_NOT_FOUND),
               "not_found"
           ) == 0);
    assert(std::strcmp(error_code_to_string(ERROR_CODE_CONFLICT), "conflict") == 0);
    assert(std::strcmp(
               error_code_to_string(ERROR_CODE_RATE_LIMITED),
               "rate_limited"
           ) == 0);
    assert(std::strcmp(
               error_code_to_string(ERROR_CODE_CONTENT_BLOCKED),
               "content_blocked"
           ) == 0);
    assert(std::strcmp(
               error_code_to_string(ERROR_CODE_DEVICE_OFFLINE),
               "device_offline"
           ) == 0);
    assert(std::strcmp(
               error_code_to_string(ERROR_CODE_SERVICE_UNAVAILABLE),
               "service_unavailable"
           ) == 0);
    assert(std::strcmp(error_code_to_string(ERROR_CODE_TIMEOUT), "timeout") == 0);
    assert(std::strcmp(error_code_to_string(ERROR_CODE_INTERNAL), "internal") == 0);
}

void test_unknown_enum_falls_back_to_internal() {
    // An out-of-contract value must never surface a raw numeric code.
    const error_code_t unknown = static_cast<error_code_t>(9999);
    assert(std::strcmp(error_code_to_string(unknown), "internal") == 0);
}

void test_esp_err_mapping() {
    assert(error_code_from_esp_err(ESP_OK) == ERROR_CODE_OK);
    assert(
        error_code_from_esp_err(ESP_ERR_INVALID_ARG) == ERROR_CODE_INVALID_REQUEST
    );
    assert(
        error_code_from_esp_err(ESP_ERR_INVALID_STATE) == ERROR_CODE_CONFLICT
    );
    assert(error_code_from_esp_err(ESP_ERR_NOT_FOUND) == ERROR_CODE_NOT_FOUND);
    assert(
        error_code_from_esp_err(ESP_ERR_NO_MEM) == ERROR_CODE_SERVICE_UNAVAILABLE
    );
    assert(error_code_from_esp_err(ESP_ERR_TIMEOUT) == ERROR_CODE_TIMEOUT);
}

void test_unmapped_esp_err_hides_internals() {
    // ESP_ERR_INVALID_SIZE and ESP_FAIL are not part of the device contract, so
    // they must collapse to ERROR_CODE_INTERNAL rather than leaking.
    assert(error_code_from_esp_err(ESP_ERR_INVALID_SIZE) == ERROR_CODE_INTERNAL);
    assert(error_code_from_esp_err(ESP_FAIL) == ERROR_CODE_INTERNAL);
    assert(error_code_from_esp_err(-12345) == ERROR_CODE_INTERNAL);
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor = error_code_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "error_code") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    assert(descriptor->shutdown == nullptr);
}

}  // namespace

int main() {
    test_contract_strings_are_stable();
    test_unknown_enum_falls_back_to_internal();
    test_esp_err_mapping();
    test_unmapped_esp_err_hides_internals();
    test_module_descriptor_is_stable();
    return 0;
}
