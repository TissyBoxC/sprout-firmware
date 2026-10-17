#include "ed25519_verify.h"

#include <string.h>

#include "monocypher-ed25519.h"

bool ed25519_verify(
    const uint8_t public_key[ED25519_VERIFY_PUBLIC_KEY_BYTES],
    const uint8_t signature[ED25519_VERIFY_SIGNATURE_BYTES],
    const uint8_t *message,
    size_t message_size
) {
    if (public_key == NULL || signature == NULL ||
        (message == NULL && message_size != 0)) {
        return false;
    }
    // Monocypher accepts a zero-length message with a non-null pointer, so
    // normalize the caller's empty input before crossing the library boundary.
    static const uint8_t empty_message = 0;
    const uint8_t *verified_message =
        message_size == 0 ? &empty_message : message;
    return crypto_ed25519_check(
        signature,
        public_key,
        verified_message,
        message_size
    ) == 0;
}
