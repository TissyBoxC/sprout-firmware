#include "provisioning_payload.h"

#include <stdio.h>
#include <string.h>

#define PROVISIONING_PAYLOAD_URI_SCHEME "sprout"
#define PROVISIONING_PAYLOAD_HOST "device"
#define PROVISIONING_PAYLOAD_SETUP_HOST "setup"
#define PROVISIONING_PAYLOAD_SECURITY_USERNAME "sprout-provisioning"

/**
 * @brief Validate an identifier that must survive an unescaped URI segment.
 *
 * Device identifiers and tokens are produced by the platform, so any other
 * character means the caller passed the wrong value and the QR must not be
 * built with it.
 */
static bool provisioning_payload_is_safe_identifier(const char *value) {
    if (value == NULL) {
        return false;
    }
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        const char character = *cursor;
        const bool is_unreserved =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.' ||
            character == ':';
        if (!is_unreserved) {
            // The platform rejects unexpected characters, so refuse to build a
            // payload that would silently bind the wrong device.
            return false;
        }
    }
    return true;
}

/**
 * @brief Percent-encode the display name.
 *
 * The product name is Chinese, so it cannot pass the identifier allowlist.
 * Encoding keeps the payload a valid URI while the guardian app still shows
 * the readable name after decoding. Returns false when the output is too small.
 */
static bool provisioning_payload_encode_name(
    const char *name,
    char *output,
    size_t output_size
) {
    static const char hex_digits[] = "0123456789ABCDEF";
    size_t written = 0;

    for (const unsigned char *cursor = (const unsigned char *)name;
         *cursor != '\0';
         ++cursor) {
        const unsigned char character = *cursor;
        const bool is_unreserved =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.' ||
            character == '~';
        if (is_unreserved) {
            if (written + 1 >= output_size) {
                return false;
            }
            output[written++] = (char)character;
            continue;
        }
        if (written + 3 >= output_size) {
            return false;
        }
        output[written++] = '%';
        output[written++] = hex_digits[(character >> 4) & 0x0F];
        output[written++] = hex_digits[character & 0x0F];
    }
    if (written >= output_size) {
        return false;
    }
    output[written] = '\0';
    return true;
}

const char *provisioning_payload_scheme(void) {
    return PROVISIONING_PAYLOAD_URI_SCHEME;
}

const char *provisioning_payload_security_username(void) {
    return PROVISIONING_PAYLOAD_SECURITY_USERNAME;
}

esp_err_t provisioning_payload_build_setup_qr(
    const char *service_name,
    const char *proof_of_possession,
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!provisioning_payload_is_safe_identifier(service_name) ||
        !provisioning_payload_is_safe_identifier(proof_of_possession)) {
        return ESP_ERR_INVALID_ARG;
    }

    const int written = snprintf(
        output,
        output_size,
        "%s://%s?service=%s&pop=%s&user=%s",
        PROVISIONING_PAYLOAD_URI_SCHEME,
        PROVISIONING_PAYLOAD_SETUP_HOST,
        service_name,
        proof_of_possession,
        PROVISIONING_PAYLOAD_SECURITY_USERNAME
    );
    if (written < 0) {
        return ESP_FAIL;
    }
    if ((size_t)written >= output_size) {
        output[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t provisioning_payload_build_qr(
    const char *device_id,
    const char *binding_token,
    const char *device_name,
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!provisioning_payload_is_safe_identifier(device_id) ||
        !provisioning_payload_is_safe_identifier(binding_token)) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *const safe_name = device_name != NULL ? device_name : "";
    char encoded_name[PROVISIONING_PAYLOAD_SIZE] = {0};
    if (!provisioning_payload_encode_name(
            safe_name,
            encoded_name,
            sizeof(encoded_name))) {
        return ESP_ERR_INVALID_SIZE;
    }
    const int written = snprintf(
        output,
        output_size,
        "%s://%s?device_id=%s&token=%s&name=%s",
        PROVISIONING_PAYLOAD_URI_SCHEME,
        PROVISIONING_PAYLOAD_HOST,
        device_id,
        binding_token,
        encoded_name
    );
    if (written < 0) {
        return ESP_FAIL;
    }
    if ((size_t)written >= output_size) {
        output[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static bool provisioning_payload_extract_query_value(
    const char *payload,
    const char *key,
    char *output,
    size_t output_size
) {
    const char *const query = strchr(payload, '?');
    if (query == NULL) {
        return false;
    }

    const size_t key_length = strlen(key);
    const char *cursor = query + 1;
    while (*cursor != '\0') {
        const char *const pair_end = strchr(cursor, '&');
        const char *const pair_stop =
            pair_end != NULL ? pair_end : cursor + strlen(cursor);
        const char *const separator = memchr(
            cursor,
            '=',
            (size_t)(pair_stop - cursor)
        );
        if (separator != NULL &&
            (size_t)(separator - cursor) == key_length &&
            strncmp(cursor, key, key_length) == 0) {
            const char *const value = separator + 1;
            const size_t value_length = (size_t)(pair_stop - value);
            if (value_length + 1 > output_size) {
                return false;
            }
            memcpy(output, value, value_length);
            output[value_length] = '\0';
            return true;
        }
        if (pair_end == NULL) {
            break;
        }
        cursor = pair_end + 1;
    }
    return false;
}

esp_err_t provisioning_payload_parse(
    const char *payload,
    char *device_id,
    size_t device_id_size,
    char *binding_token,
    size_t binding_token_size
) {
    if (payload == NULL || device_id == NULL || binding_token == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *const prefix = PROVISIONING_PAYLOAD_URI_SCHEME "://" PROVISIONING_PAYLOAD_HOST;
    if (strncmp(payload, prefix, strlen(prefix)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!provisioning_payload_extract_query_value(
            payload,
            "device_id",
            device_id,
            device_id_size) ||
        !provisioning_payload_extract_query_value(
            payload,
            "token",
            binding_token,
            binding_token_size)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device_id[0] == '\0' || binding_token[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t provisioning_payload_parse_setup_qr(
    const char *payload,
    char *service_name,
    size_t service_name_size,
    char *proof_of_possession,
    size_t proof_of_possession_size
) {
    if (payload == NULL || service_name == NULL ||
        proof_of_possession == NULL || service_name_size == 0 ||
        proof_of_possession_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *const prefix =
        PROVISIONING_PAYLOAD_URI_SCHEME "://" PROVISIONING_PAYLOAD_SETUP_HOST;
    if (strncmp(payload, prefix, strlen(prefix)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!provisioning_payload_extract_query_value(
            payload,
            "service",
            service_name,
            service_name_size) ||
        !provisioning_payload_extract_query_value(
            payload,
            "pop",
            proof_of_possession,
            proof_of_possession_size)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (service_name[0] == '\0' || proof_of_possession[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t provisioning_payload_initialize(void) {
    return ESP_OK;
}

const module_descriptor_t *provisioning_payload_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "provisioning_payload",
        .version = "1.0.0",
        .initialize = provisioning_payload_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
