#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ED25519_VERIFY_PUBLIC_KEY_BYTES 32U
#define ED25519_VERIFY_SIGNATURE_BYTES 64U

/**
 * @brief Verify an RFC 8032 Ed25519 signature over one exact byte string.
 *
 * The function is side-effect free and never accepts a malleable signature.
 * Inputs are borrowed for the duration of the call.
 */
bool ed25519_verify(
    const uint8_t public_key[ED25519_VERIFY_PUBLIC_KEY_BYTES],
    const uint8_t signature[ED25519_VERIFY_SIGNATURE_BYTES],
    const uint8_t *message,
    size_t message_size
);

#ifdef __cplusplus
}
#endif
