#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stable machine-readable error codes shared with platform contracts.
 *
 * The enum values are internal; network payloads use the string returned by
 * error_code_to_string. Do not expose ESP-IDF error numbers to the platform.
 */
typedef enum {
    ERROR_CODE_OK = 0,
    ERROR_CODE_INVALID_REQUEST,
    ERROR_CODE_UNAUTHENTICATED,
    ERROR_CODE_PERMISSION_DENIED,
    ERROR_CODE_NOT_FOUND,
    ERROR_CODE_CONFLICT,
    ERROR_CODE_RATE_LIMITED,
    ERROR_CODE_CONTENT_BLOCKED,
    ERROR_CODE_DEVICE_OFFLINE,
    ERROR_CODE_SERVICE_UNAVAILABLE,
    ERROR_CODE_TIMEOUT,
    ERROR_CODE_INTERNAL,
} error_code_t;

/** @brief Return the stable contract string for one error code. */
const char *error_code_to_string(error_code_t error_code);

/**
 * @brief Map an ESP-IDF result to a platform error code.
 *
 * Unknown ESP-IDF errors map to ERROR_CODE_INTERNAL so firmware never leaks
 * implementation-specific error numbers across the device API.
 */
error_code_t error_code_from_esp_err(esp_err_t esp_error);

/** @brief Return the removable-module descriptor for error_code. */
const module_descriptor_t *error_code_module_descriptor(void);

#ifdef __cplusplus
}
#endif
