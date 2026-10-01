#include "device_identity.h"

#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "psa/crypto.h"

#define DEVICE_IDENTIFIER_SHA256_BYTES 32
#define DEVICE_IDENTIFIER_PREFIX_BYTES 8

static bool identity_is_initialized;
static device_identity_t identity_snapshot;

/**
 * @brief Derive the privacy-preserving device identifier.
 *
 * The factory MAC is unique but must not be exposed in device payloads. The
 * first bytes of its SHA-256 digest keep the identifier stable while removing
 * the direct chip-address meaning.
 */
static esp_err_t device_identity_derive_identifier(void) {
    uint8_t factory_mac[6] = {0};
    const esp_err_t mac_result = esp_efuse_mac_get_default(factory_mac);
    if (mac_result != ESP_OK) {
        return mac_result;
    }

    const psa_status_t crypto_result = psa_crypto_init();
    if (crypto_result != PSA_SUCCESS) {
        return ESP_FAIL;
    }

    uint8_t identifier_digest[DEVICE_IDENTIFIER_SHA256_BYTES] = {0};
    size_t digest_length = 0;
    const psa_status_t digest_result = psa_hash_compute(
        PSA_ALG_SHA_256,
        factory_mac,
        sizeof(factory_mac),
        identifier_digest,
        sizeof(identifier_digest),
        &digest_length
    );
    if (digest_result != PSA_SUCCESS ||
        digest_length != sizeof(identifier_digest)) {
        memset(factory_mac, 0, sizeof(factory_mac));
        memset(identifier_digest, 0, sizeof(identifier_digest));
        return ESP_FAIL;
    }

    char digest_text[DEVICE_IDENTIFIER_PREFIX_BYTES * 2 + 1] = {0};
    for (size_t index = 0; index < DEVICE_IDENTIFIER_PREFIX_BYTES; ++index) {
        const int formatted = snprintf(
            digest_text + (index * 2),
            sizeof(digest_text) - (index * 2),
            "%02x",
            identifier_digest[index]
        );
        if (formatted != 2) {
            memset(factory_mac, 0, sizeof(factory_mac));
            memset(identifier_digest, 0, sizeof(identifier_digest));
            return ESP_ERR_INVALID_SIZE;
        }
    }

    const int formatted = snprintf(
        identity_snapshot.device_id,
        sizeof(identity_snapshot.device_id),
        "device_%s",
        digest_text
    );

    // Clear raw chip material before returning any error or success.
    memset(factory_mac, 0, sizeof(factory_mac));
    memset(identifier_digest, 0, sizeof(identifier_digest));
    memset(digest_text, 0, sizeof(digest_text));

    if (formatted <= 0 || (size_t)formatted >= sizeof(identity_snapshot.device_id)) {
        memset(&identity_snapshot, 0, sizeof(identity_snapshot));
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t device_identity_initialize(void) {
    if (identity_is_initialized) {
        return ESP_OK;
    }

    const esp_err_t result = device_identity_derive_identifier();
    if (result != ESP_OK) {
        return result;
    }

    identity_is_initialized = true;
    return ESP_OK;
}

device_identity_t device_identity_get(void) {
    if (!identity_is_initialized) {
        (void)device_identity_initialize();
    }
    return identity_snapshot;
}

esp_err_t device_identity_copy(char *output, size_t output_size) {
    if (output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device_identity_initialize() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (output_size < sizeof(identity_snapshot.device_id)) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(output, identity_snapshot.device_id, sizeof(identity_snapshot.device_id));
    return ESP_OK;
}

const module_descriptor_t *device_identity_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "device_identity",
        .version = "1.0.0",
        .initialize = device_identity_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
