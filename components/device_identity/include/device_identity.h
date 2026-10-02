#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Buffer size required for a null-terminated device identifier. */
#define DEVICE_IDENTIFIER_SIZE 32

/** @brief Uncompressed ECDSA P-256 public-key size in bytes. */
#define DEVICE_IDENTITY_PUBLIC_KEY_BYTES 65

/** @brief ECDSA P-256 signature size in bytes, encoded as r || s. */
#define DEVICE_IDENTITY_SIGNATURE_BYTES 64

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

/**
 * @brief Copy the device's uncompressed ECDSA P-256 public key.
 *
 * The private key is generated once in encrypted NVS and never leaves the
 * device. Registration and challenge authentication use this public key.
 */
esp_err_t device_identity_copy_public_key(
    uint8_t *output,
    size_t output_size
);

/**
 * @brief Sign a platform challenge with the device's ECDSA P-256 private key.
 *
 * The caller owns the message buffer. The signature is written as a raw
 * 64-byte r || s signature suitable for Base64 transport. The platform hashes
 * the message with SHA-256 before verification.
 */
esp_err_t device_identity_sign(
    const uint8_t *message,
    size_t message_size,
    uint8_t *signature,
    size_t signature_size
);

/** @brief Return the removable-module descriptor for device_identity. */
const module_descriptor_t *device_identity_module_descriptor(void);

#ifdef __cplusplus
}
#endif
