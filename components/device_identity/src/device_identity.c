#include "device_identity.h"

#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "nvs_flash.h"
#include "psa/crypto.h"

#define DEVICE_IDENTIFIER_SHA256_BYTES 32
#define DEVICE_IDENTIFIER_PREFIX_BYTES 8
#define DEVICE_IDENTITY_NVS_NAMESPACE "sprout_identity"
#define DEVICE_IDENTITY_NVS_PRIVATE_KEY "p256_private"
#define DEVICE_IDENTITY_PRIVATE_KEY_BYTES 32

static bool identity_is_initialized;
static device_identity_t identity_snapshot;
static uint8_t identity_private_key[DEVICE_IDENTITY_PRIVATE_KEY_BYTES];
static uint8_t identity_public_key[DEVICE_IDENTITY_PUBLIC_KEY_BYTES];

static esp_err_t device_identity_load_or_create_key(void) {
    nvs_handle_t nvs_handle = 0;
    esp_err_t result = nvs_flash_init_partition(
        CONFIG_DEVICE_IDENTITY_NVS_PARTITION
    );
    if (result != ESP_OK && result != ESP_ERR_NVS_NO_FREE_PAGES &&
        result != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        return result;
    }
    if (result != ESP_OK) {
        result = nvs_flash_erase_partition(
            CONFIG_DEVICE_IDENTITY_NVS_PARTITION
        );
        if (result != ESP_OK) {
            return result;
        }
        result = nvs_flash_init_partition(
            CONFIG_DEVICE_IDENTITY_NVS_PARTITION
        );
        if (result != ESP_OK) {
            return result;
        }
    }

    result = nvs_open_from_partition(
        CONFIG_DEVICE_IDENTITY_NVS_PARTITION,
        DEVICE_IDENTITY_NVS_NAMESPACE,
        NVS_READWRITE,
        &nvs_handle
    );
    if (result != ESP_OK) {
        return result;
    }

    bool should_persist_key = false;
    size_t private_key_size = sizeof(identity_private_key);
    result = nvs_get_blob(
        nvs_handle,
        DEVICE_IDENTITY_NVS_PRIVATE_KEY,
        identity_private_key,
        &private_key_size
    );
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_type(
            &attributes,
            PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)
        );
        psa_set_key_bits(&attributes, 256);
        psa_set_key_usage_flags(
            &attributes,
            PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_MESSAGE
        );
        psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));

        psa_key_id_t generated_key = 0;
        psa_status_t crypto_result = psa_generate_key(
            &attributes,
            &generated_key
        );
        if (crypto_result == PSA_SUCCESS) {
            size_t exported_size = 0;
            crypto_result = psa_export_key(
                generated_key,
                identity_private_key,
                sizeof(identity_private_key),
                &exported_size
            );
            if (crypto_result == PSA_SUCCESS &&
                exported_size == sizeof(identity_private_key)) {
                should_persist_key = true;
                result = ESP_OK;
            } else {
                result = ESP_FAIL;
            }
            (void)psa_destroy_key(generated_key);
        } else {
            result = ESP_FAIL;
        }
    } else if (result == ESP_OK &&
               private_key_size != sizeof(identity_private_key)) {
        result = ESP_ERR_INVALID_SIZE;
    }

    if (result != ESP_OK) {
        nvs_close(nvs_handle);
        memset(identity_private_key, 0, sizeof(identity_private_key));
        return result;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(
        &attributes,
        PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)
    );
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(
        &attributes,
        PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_MESSAGE
    );
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t imported_key = 0;
    const psa_status_t import_result = psa_import_key(
        &attributes,
        identity_private_key,
        sizeof(identity_private_key),
        &imported_key
    );
    if (import_result != PSA_SUCCESS) {
        nvs_close(nvs_handle);
        memset(identity_private_key, 0, sizeof(identity_private_key));
        return ESP_FAIL;
    }

    size_t public_key_size = 0;
    const psa_status_t export_result = psa_export_public_key(
        imported_key,
        identity_public_key,
        sizeof(identity_public_key),
        &public_key_size
    );
    const bool public_key_is_valid =
        export_result == PSA_SUCCESS &&
        public_key_size == sizeof(identity_public_key);
    if (public_key_is_valid && should_persist_key) {
        result = nvs_set_blob(
            nvs_handle,
            DEVICE_IDENTITY_NVS_PRIVATE_KEY,
            identity_private_key,
            sizeof(identity_private_key)
        );
        if (result == ESP_OK) {
            result = nvs_commit(nvs_handle);
        }
    }
    (void)psa_destroy_key(imported_key);
    nvs_close(nvs_handle);

    if (!public_key_is_valid || result != ESP_OK) {
        memset(identity_private_key, 0, sizeof(identity_private_key));
        memset(identity_public_key, 0, sizeof(identity_public_key));
        return result == ESP_OK ? ESP_FAIL : result;
    }
    return ESP_OK;
}

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

    const psa_status_t crypto_result = psa_crypto_init();
    if (crypto_result != PSA_SUCCESS) {
        return ESP_FAIL;
    }

    const esp_err_t result = device_identity_derive_identifier();
    if (result != ESP_OK) {
        return result;
    }

    const esp_err_t key_result = device_identity_load_or_create_key();
    if (key_result != ESP_OK) {
        return key_result;
    }

    identity_is_initialized = true;
    return ESP_OK;
}

device_identity_t device_identity_get(void) {
    if (device_identity_initialize() != ESP_OK) {
        memset(&identity_snapshot, 0, sizeof(identity_snapshot));
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

esp_err_t device_identity_copy_public_key(
    uint8_t *output,
    size_t output_size
) {
    if (output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device_identity_initialize() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (output_size < sizeof(identity_public_key)) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(output, identity_public_key, sizeof(identity_public_key));
    return ESP_OK;
}

esp_err_t device_identity_sign(
    const uint8_t *message,
    size_t message_size,
    uint8_t *signature,
    size_t signature_size
) {
    if (message == NULL || signature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device_identity_initialize() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (signature_size < DEVICE_IDENTITY_SIGNATURE_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(
        &attributes,
        PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)
    );
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t signing_key = 0;
    const psa_status_t import_result = psa_import_key(
        &attributes,
        identity_private_key,
        sizeof(identity_private_key),
        &signing_key
    );
    if (import_result != PSA_SUCCESS) {
        return ESP_FAIL;
    }

    size_t output_size = 0;
    const psa_status_t sign_result = psa_sign_message(
        signing_key,
        PSA_ALG_ECDSA(PSA_ALG_SHA_256),
        message,
        message_size,
        signature,
        signature_size,
        &output_size
    );
    (void)psa_destroy_key(signing_key);
    if (sign_result != PSA_SUCCESS ||
        output_size != DEVICE_IDENTITY_SIGNATURE_BYTES) {
        memset(signature, 0, signature_size);
        return ESP_FAIL;
    }
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
