#include "error_code.h"

const char *error_code_to_string(error_code_t error_code) {
    switch (error_code) {
        case ERROR_CODE_OK:
            return "ok";
        case ERROR_CODE_INVALID_REQUEST:
            return "invalid_request";
        case ERROR_CODE_UNAUTHENTICATED:
            return "unauthenticated";
        case ERROR_CODE_PERMISSION_DENIED:
            return "permission_denied";
        case ERROR_CODE_NOT_FOUND:
            return "not_found";
        case ERROR_CODE_CONFLICT:
            return "conflict";
        case ERROR_CODE_RATE_LIMITED:
            return "rate_limited";
        case ERROR_CODE_CONTENT_BLOCKED:
            return "content_blocked";
        case ERROR_CODE_DEVICE_OFFLINE:
            return "device_offline";
        case ERROR_CODE_SERVICE_UNAVAILABLE:
            return "service_unavailable";
        case ERROR_CODE_TIMEOUT:
            return "timeout";
        case ERROR_CODE_INTERNAL:
        default:
            return "internal";
    }
}

error_code_t error_code_from_esp_err(esp_err_t esp_error) {
    switch (esp_error) {
        case ESP_OK:
            return ERROR_CODE_OK;
        case ESP_ERR_INVALID_ARG:
            return ERROR_CODE_INVALID_REQUEST;
        case ESP_ERR_INVALID_STATE:
            return ERROR_CODE_CONFLICT;
        case ESP_ERR_NOT_FOUND:
            return ERROR_CODE_NOT_FOUND;
        case ESP_ERR_NO_MEM:
            return ERROR_CODE_SERVICE_UNAVAILABLE;
        case ESP_ERR_TIMEOUT:
            return ERROR_CODE_TIMEOUT;
        default:
            return ERROR_CODE_INTERNAL;
    }
}

static esp_err_t error_code_initialize(void) {
    return ESP_OK;
}

const module_descriptor_t *error_code_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "error_code",
        .version = "1.0.0",
        .initialize = error_code_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
