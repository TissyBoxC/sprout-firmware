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

/** @brief Maximum length of the SRP username excluding the terminator. */
#define PROVISIONING_PAYLOAD_USERNAME_SIZE 64

/** @brief Return the URI scheme shared with the parent application. */
const char *provisioning_payload_scheme(void);

/**
 * @brief Return the fixed SRP username expected by the guardian application.
 *
 * Security 2 authenticates the provisioning session with both this username
 * and the per-device proof of possession. The username identifies the
 * provisioning protocol endpoint, while the PoP stays unique per device.
 */
const char *provisioning_payload_security_username(void);

/**
 * @brief Build the first-run setup URI shown by a display-equipped device.
 *
 * The URI contains only the locally scoped service name and a temporary
 * proof of possession. It never contains a platform token, because the
 * platform token is issued only after the device has joined Wi-Fi and
 * authenticated. The guardian application uses this payload to find the
 * device, join it to Wi-Fi, and then read the one-time binding URI from the
 * local provisioning session.
 *
 * Returns ESP_ERR_INVALID_ARG when an identifier is missing or unsafe, and
 * ESP_ERR_INVALID_SIZE when the output buffer is too small.
 */
esp_err_t provisioning_payload_build_setup_qr(
    const char *service_name,
    const char *proof_of_possession,
    char *output,
    size_t output_size
);

/**
 * @brief Parse the first-run setup URI produced by build_setup_qr.
 *
 * The guardian application and the device display use the same format, so a
 * signed or cached payload can be validated before any local connection is
 * attempted. Absent or malformed fields are rejected.
 */
esp_err_t provisioning_payload_parse_setup_qr(
    const char *payload,
    char *service_name,
    size_t service_name_size,
    char *proof_of_possession,
    size_t proof_of_possession_size
);

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
