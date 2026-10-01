#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Buffer size required for a null-terminated device identifier. */
#define DEVICE_IDENTIFIER_SIZE 32

/** @brief Immutable identity derived from the device's factory MAC. */
typedef struct {
    char device_id[DEVICE_IDENTIFIER_SIZE];
} device_identity_t;

/**
 * @brief Return the device identity used by platform registration and auth.
 *
 * The identifier is derived from the factory Wi-Fi station MAC and does not
 * expose the raw MAC in payloads. The returned value is stable for one chip.
 */
device_identity_t device_identity_get(void);

/**
 * @brief Copy the device identifier into a caller-owned buffer.
 *
 * Returns ESP_ERR_INVALID_SIZE when the output buffer cannot hold the
 * null-terminated identifier.
 */
esp_err_t device_identity_copy(char *output, size_t output_size);

/** @brief Return the removable-module descriptor for device_identity. */
const module_descriptor_t *device_identity_module_descriptor(void);

#ifdef __cplusplus
}
#endif
