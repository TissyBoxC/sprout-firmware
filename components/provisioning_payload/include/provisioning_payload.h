#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum encoded payload length including the null terminator.
 *
 * The platform binding token is 64 hexadecimal characters. The remaining
 * budget covers the scheme, device identifier, name, and escaping.
 */
#define PROVISIONING_PAYLOAD_SIZE 240

/** @brief Return the URI scheme shared with the parent application. */
const char *provisioning_payload_scheme(void);

/**
 * @brief Build the QR payload a guardian scans to bind this device.
 *
 * Only the single-use binding token, device identifier, and display name are
 * encoded. No credential, key, or guardian identity is ever placed in the
 * code. Returns ESP_ERR_INVALID_ARG for a missing token or device identifier
 * and ESP_ERR_INVALID_SIZE when the output buffer is too small.
 */
esp_err_t provisioning_payload_build_qr(
    const char *device_id,
    const char *binding_token,
    const char *device_name,
    char *output,
    size_t output_size
);

/**
 * @brief Parse the subset of a QR payload the device can validate.
 *
 * Implemented for the provisioning access-point flow, where the device may
 * need to echo back its own identifier. Absent fields are treated as invalid.
 */
esp_err_t provisioning_payload_parse(
    const char *payload,
    char *device_id,
    size_t device_id_size,
    char *binding_token,
    size_t binding_token_size
);

/** @brief Return the removable-module descriptor for provisioning_payload. */
const module_descriptor_t *provisioning_payload_module_descriptor(void);

#ifdef __cplusplus
}
#endif
