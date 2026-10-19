#include "ota_manifest.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ed25519_verify.h"

#define OTA_MANIFEST_BASE64_DECODED_SIGNATURE_SIZE 64
#define OTA_MANIFEST_BASE64_PUBLIC_KEY_SIZE 32
#define OTA_MANIFEST_PUBLIC_KEY_TEXT_SIZE 48
#define OTA_MANIFEST_CANONICAL_PAYLOAD_SIZE 3072
#define OTA_MANIFEST_URL_MAX_BYTES 512U

typedef struct {
    char key_id[OTA_MANIFEST_KEY_ID_SIZE];
    char public_key[OTA_MANIFEST_PUBLIC_KEY_TEXT_SIZE];
} ota_manifest_keyring_entry_t;

typedef struct {
    const char *cursor;
    const char *end;
    ota_manifest_error_t error;
} ota_json_cursor_t;

static void ota_json_skip_whitespace(ota_json_cursor_t *cursor) {
    while (cursor->cursor < cursor->end &&
           isspace((unsigned char)*cursor->cursor)) {
        ++cursor->cursor;
    }
}

static bool ota_json_expect(ota_json_cursor_t *cursor, char expected) {
    ota_json_skip_whitespace(cursor);
    if (cursor->cursor >= cursor->end ||
        *cursor->cursor != expected) {
        cursor->error = OTA_MANIFEST_ERR_JSON;
        return false;
    }
    ++cursor->cursor;
    return true;
}

static bool ota_json_parse_string(
    ota_json_cursor_t *cursor,
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size == 0) {
        cursor->error = OTA_MANIFEST_ERR_JSON;
        return false;
    }
    ota_json_skip_whitespace(cursor);
    if (cursor->cursor >= cursor->end || *cursor->cursor != '"') {
        cursor->error = OTA_MANIFEST_ERR_JSON;
        return false;
    }
    ++cursor->cursor;
    size_t output_length = 0;
    while (cursor->cursor < cursor->end) {
        const unsigned char value = (unsigned char)*cursor->cursor++;
        if (value == '"') {
            output[output_length] = '\0';
            return true;
        }
        if (value < 0x20) {
            cursor->error = OTA_MANIFEST_ERR_JSON;
            return false;
        }
        if (value == '\\') {
            if (cursor->cursor >= cursor->end) {
                cursor->error = OTA_MANIFEST_ERR_JSON;
                return false;
            }
            const char escaped = *cursor->cursor++;
            char decoded = '\0';
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    decoded = escaped;
                    break;
                case 'b':
                    decoded = '\b';
                    break;
                case 'f':
                    decoded = '\f';
                    break;
                case 'n':
                    decoded = '\n';
                    break;
                case 'r':
                    decoded = '\r';
                    break;
                case 't':
                    decoded = '\t';
                    break;
                default:
                    // Technical OTA fields are ASCII. Rejecting \u keeps the
                    // reader simple and prevents unicode spoofing in IDs.
                    cursor->error = OTA_MANIFEST_ERR_JSON;
                    return false;
            }
            if (output_length + 1 >= output_size) {
                cursor->error = OTA_MANIFEST_ERR_JSON;
                return false;
            }
            output[output_length++] = decoded;
            continue;
        }
        if (output_length + 1 >= output_size) {
            cursor->error = OTA_MANIFEST_ERR_JSON;
            return false;
        }
        output[output_length++] = (char)value;
    }
    cursor->error = OTA_MANIFEST_ERR_JSON;
    return false;
}

static bool ota_json_parse_unsigned(
    ota_json_cursor_t *cursor,
    uint64_t *value_out
) {
    ota_json_skip_whitespace(cursor);
    if (cursor->cursor >= cursor->end ||
        !isdigit((unsigned char)*cursor->cursor)) {
        cursor->error = OTA_MANIFEST_ERR_JSON;
        return false;
    }
    uint64_t value = 0;
    while (cursor->cursor < cursor->end &&
           isdigit((unsigned char)*cursor->cursor)) {
        const uint64_t digit = (uint64_t)(*cursor->cursor - '0');
        if (value > (UINT64_MAX - digit) / 10U) {
            cursor->error = OTA_MANIFEST_ERR_JSON;
            return false;
        }
        value = value * 10U + digit;
        ++cursor->cursor;
    }
    *value_out = value;
    return true;
}

static bool ota_json_parse_bool(
    ota_json_cursor_t *cursor,
    bool *value_out
) {
    ota_json_skip_whitespace(cursor);
    const size_t remaining = (size_t)(cursor->end - cursor->cursor);
    if (remaining >= 4 && memcmp(cursor->cursor, "true", 4) == 0) {
        cursor->cursor += 4;
        *value_out = true;
        return true;
    }
    if (remaining >= 5 && memcmp(cursor->cursor, "false", 5) == 0) {
        cursor->cursor += 5;
        *value_out = false;
        return true;
    }
    cursor->error = OTA_MANIFEST_ERR_JSON;
    return false;
}

static bool ota_manifest_validate_string_field(
    const char *value,
    size_t maximum_length
) {
    return value != NULL && value[0] != '\0' &&
        strlen(value) <= maximum_length;
}

static bool ota_manifest_semver_is_valid(const char *value) {
    if (value == NULL || value[0] == '\0' ||
        strlen(value) > OTA_MANIFEST_VERSION_SIZE - 1) {
        return false;
    }
    const char *cursor = value;
    for (int component = 0; component < 3; ++component) {
        if (!isdigit((unsigned char)*cursor)) {
            return false;
        }
        while (isdigit((unsigned char)*cursor)) {
            ++cursor;
        }
        if (component < 2) {
            if (*cursor != '.') {
                return false;
            }
            ++cursor;
        }
    }
    if (*cursor == '-') {
        ++cursor;
        if (*cursor == '\0') {
            return false;
        }
        while (*cursor != '\0' && *cursor != '+') {
            if (!isalnum((unsigned char)*cursor) &&
                *cursor != '-' && *cursor != '.') {
                return false;
            }
            ++cursor;
        }
    }
    if (*cursor == '+') {
        ++cursor;
        if (*cursor == '\0') {
            return false;
        }
        while (*cursor != '\0') {
            if (!isalnum((unsigned char)*cursor) &&
                *cursor != '-' && *cursor != '.') {
                return false;
            }
            ++cursor;
        }
    }
    return *cursor == '\0';
}

static bool ota_manifest_copy_field(
    char *destination,
    size_t destination_size,
    const char *source
) {
    if (destination == NULL || destination_size == 0 || source == NULL) {
        return false;
    }
    const size_t length = strlen(source);
    if (length >= destination_size) {
        return false;
    }
    memcpy(destination, source, length + 1);
    return true;
}

static int ota_manifest_base64_value(char value) {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }
    if (value == '=') {
        return -2;
    }
    if (value == '+') {
        return 62;
    }
    if (value == '/') {
        return 63;
    }
    return -1;
}

static bool ota_manifest_base64_decode(
    const char *encoded,
    uint8_t *output,
    size_t output_size,
    size_t *decoded_size_out
) {
    if (encoded == NULL || output == NULL || decoded_size_out == NULL) {
        return false;
    }
    const size_t encoded_size = strlen(encoded);
    if (encoded_size == 0 || encoded_size % 4 != 0) {
        return false;
    }
    size_t padding = 0;
    if (encoded[encoded_size - 1] == '=') {
        ++padding;
    }
    if (encoded[encoded_size - 2] == '=') {
        ++padding;
    }
    if (padding > 2) {
        return false;
    }
    const size_t decoded_size = encoded_size / 4 * 3 - padding;
    if (decoded_size > output_size) {
        return false;
    }
    size_t output_index = 0;
    for (size_t input_index = 0;
         input_index < encoded_size;
         input_index += 4) {
        const bool last_group = input_index + 4 == encoded_size;
        const int first = ota_manifest_base64_value(encoded[input_index]);
        const int second = ota_manifest_base64_value(encoded[input_index + 1]);
        const char third_character = encoded[input_index + 2];
        const char fourth_character = encoded[input_index + 3];
        const int third = ota_manifest_base64_value(third_character);
        const int fourth = ota_manifest_base64_value(fourth_character);
        if (first < 0 || second < 0 ||
            (third < 0 && !(last_group && third == -2)) ||
            (fourth < 0 && !(last_group && fourth == -2))) {
            return false;
        }
        if (third == -2 && fourth != -2) {
            return false;
        }
        output[output_index++] = (uint8_t)(
            ((unsigned)first << 2) | ((unsigned)second >> 4)
        );
        if (third != -2) {
            if (output_index >= decoded_size + 1) {
                return false;
            }
            output[output_index++] = (uint8_t)(
                ((unsigned)second << 4) | ((unsigned)third >> 2)
            );
        }
        if (fourth != -2) {
            if (output_index >= decoded_size + 1) {
                return false;
            }
            output[output_index++] = (uint8_t)(
                ((unsigned)third << 6) | (unsigned)fourth
            );
        }
    }
    if (output_index != decoded_size) {
        return false;
    }
    *decoded_size_out = decoded_size;
    return true;
}

static bool ota_manifest_canonical_base64(
    const char *encoded,
    const uint8_t *decoded,
    size_t decoded_size
) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (encoded == NULL || decoded == NULL ||
        strlen(encoded) != (decoded_size + 2) / 3 * 4) {
        return false;
    }
    size_t encoded_index = 0;
    for (size_t index = 0; index < decoded_size; index += 3) {
        const size_t remaining = decoded_size - index;
        const uint32_t block =
            ((uint32_t)decoded[index] << 16) |
            (remaining > 1 ? ((uint32_t)decoded[index + 1] << 8) : 0U) |
            (remaining > 2 ? (uint32_t)decoded[index + 2] : 0U);
        if (encoded[encoded_index++] != alphabet[(block >> 18) & 0x3FU] ||
            encoded[encoded_index++] != alphabet[(block >> 12) & 0x3FU]) {
            return false;
        }
        if (remaining > 1) {
            if (encoded[encoded_index++] != alphabet[(block >> 6) & 0x3FU]) {
                return false;
            }
        } else if (encoded[encoded_index++] != '=') {
            return false;
        }
        if (remaining > 2) {
            if (encoded[encoded_index++] != alphabet[block & 0x3FU]) {
                return false;
            }
        } else if (encoded[encoded_index++] != '=') {
            return false;
        }
    }
    return encoded[encoded_index] == '\0';
}

static bool ota_manifest_append_canonical_field(
    char *payload,
    size_t payload_size,
    size_t *length_out,
    const char *name,
    const char *value
) {
    if (payload == NULL || payload_size == 0 || length_out == NULL ||
        name == NULL || value == NULL) {
        return false;
    }
    const int written = snprintf(
        payload + *length_out,
        payload_size - *length_out,
        "%s=%s\n",
        name,
        value
    );
    if (written < 0 ||
        (size_t)written >= payload_size - *length_out) {
        return false;
    }
    *length_out += (size_t)written;
    return true;
}

static bool ota_manifest_build_signature_payload(
    const ota_manifest_t *manifest,
    char *payload,
    size_t payload_size,
    size_t *payload_length_out
) {
    if (manifest == NULL || payload == NULL || payload_size == 0 ||
        payload_length_out == NULL || manifest->sha256[0] == '\0' ||
        strcmp(
            manifest->signature_payload_version,
            OTA_MANIFEST_SIGNATURE_PAYLOAD_VERSION
        ) != 0) {
        return false;
    }
    char normalized_sha256[OTA_MANIFEST_HASH_SIZE];
    for (size_t index = 0; index < 64; ++index) {
        normalized_sha256[index] =
            (char)tolower((unsigned char)manifest->sha256[index]);
    }
    normalized_sha256[64] = '\0';
    char size_bytes[21];
    const int size_bytes_length = snprintf(
        size_bytes,
        sizeof(size_bytes),
        "%llu",
        (unsigned long long)manifest->size_bytes
    );
    if (size_bytes_length <= 0 ||
        (size_t)size_bytes_length >= sizeof(size_bytes)) {
        return false;
    }
    size_t length = 0;
    payload[0] = '\0';
    if (
        !ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "schema_version",
            OTA_MANIFEST_SIGNATURE_PAYLOAD_VERSION
        ) || (
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "release_id",
            manifest->release_id
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "firmware_version",
            manifest->firmware_version
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "hardware_revision",
            manifest->hardware_revision
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "channel",
            manifest->channel
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "artifact_key",
            manifest->artifact_key
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "artifact_url",
            manifest->artifact_url
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "sha256",
            normalized_sha256
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "size_bytes",
            size_bytes
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "signature_key_id",
            manifest->signature_key_id
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "signature_algorithm",
            manifest->signature_algorithm
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "rollback_allowed",
            manifest->rollback_allowed ? "true" : "false"
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "mandatory",
            manifest->mandatory ? "true" : "false"
        ) &&
        ota_manifest_append_canonical_field(
            payload,
            payload_size,
            &length,
            "min_source_version",
            manifest->min_source_version
        ) == false)) {
        memset(normalized_sha256, 0, sizeof(normalized_sha256));
        return false;
    }
    memset(normalized_sha256, 0, sizeof(normalized_sha256));
    if (length == 0 || payload[length - 1] != '\n') {
        return false;
    }
    *payload_length_out = length;
    return true;
}

static bool ota_manifest_channel_is_valid(const char *channel) {
    return strcmp(channel, "stable") == 0 ||
        strcmp(channel, "canary") == 0 ||
        strcmp(channel, "internal") == 0;
}

static bool ota_manifest_signature_algorithm_is_ed25519(
    const char *algorithm
) {
    if (algorithm == NULL || strlen(algorithm) != strlen("ed25519")) {
        return false;
    }
    for (size_t index = 0; index < strlen("ed25519"); ++index) {
        if (tolower((unsigned char)algorithm[index]) != "ed25519"[index]) {
            return false;
        }
    }
    return true;
}

static bool ota_manifest_url_is_https(const char *url) {
    if (url == NULL || strncmp(url, "https://", 8) != 0) {
        return false;
    }
    const size_t url_length = strlen(url);
    if (url_length < 10 || url_length > OTA_MANIFEST_URL_MAX_BYTES) {
        return false;
    }
    for (size_t index = 0; index < url_length; ++index) {
        const unsigned char byte = (unsigned char)url[index];
        if (byte < 0x21 || byte > 0x7e) {
            return false;
        }
    }
    if (strchr(url, '?') != NULL ||
        strchr(url, '#') != NULL ||
        strchr(url, '%') != NULL ||
        strchr(url, '\\') != NULL) {
        return false;
    }
    const char *authority = url + 8;
    const char *path = strchr(authority, '/');
    if (path == NULL || path == authority ||
        memchr(authority, '@', (size_t)(path - authority)) != NULL ||
        path[1] == '\0' ||
        path[strlen(path) - 1] == '/') {
        return false;
    }
    const char *segment = path + 1;
    for (const char *cursor = segment; ; ++cursor) {
        if (*cursor != '/' && *cursor != '\0') {
            continue;
        }
        const size_t segment_length = (size_t)(cursor - segment);
        if (segment_length == 0 ||
            (segment_length == 1 && segment[0] == '.') ||
            (segment_length == 2 &&
             segment[0] == '.' && segment[1] == '.')) {
            return false;
        }
        if (*cursor == '\0') {
            return true;
        }
        segment = cursor + 1;
    }
}

static bool ota_manifest_artifact_key_is_valid(const char *artifact_key) {
    if (artifact_key == NULL || artifact_key[0] == '\0' ||
        artifact_key[0] == '/' ||
        strlen(artifact_key) > OTA_MANIFEST_ARTIFACT_KEY_SIZE - 1) {
        return false;
    }
    const char *segment = artifact_key;
    for (const char *cursor = artifact_key; ; ++cursor) {
        const char character = *cursor;
        if (character == '/' || character == '\0') {
            const size_t segment_length = (size_t)(cursor - segment);
            if (segment_length == 0 ||
                (segment_length == 1 && segment[0] == '.')) {
                return false;
            }
            if (segment_length == 2 &&
                segment[0] == '.' && segment[1] == '.') {
                return false;
            }
            if (character == '\0') {
                return true;
            }
            segment = cursor + 1;
            continue;
        }
        const unsigned char byte = (unsigned char)character;
        if (byte < 0x21 || byte > 0x7e ||
            character == '\\' || character == '?' ||
            character == '#' || character == '%') {
            return false;
        }
    }
}

static bool ota_manifest_artifact_key_matches_url(
    const char *artifact_key,
    const char *artifact_url
) {
    if (artifact_key == NULL || artifact_url == NULL ||
        artifact_key[0] == '\0' || artifact_url[0] == '\0' ||
        strchr(artifact_url, '?') != NULL ||
        strchr(artifact_url, '#') != NULL ||
        strchr(artifact_url, '%') != NULL ||
        strchr(artifact_url, '\\') != NULL) {
        return false;
    }
    const char *authority = artifact_url + 8;
    const char *path = strchr(authority, '/');
    if (path == NULL ||
        memchr(authority, '@', (size_t)(path - authority)) != NULL) {
        return false;
    }
    if (path[1] == '\0' || path[strlen(path) - 1] == '/') {
        return false;
    }
    ++path;
    const size_t key_length = strlen(artifact_key);
    const size_t path_length = strlen(path);
    if (path_length < key_length ||
        strcmp(path + (path_length - key_length), artifact_key) != 0) {
        return false;
    }
    return path_length == key_length ||
        path[path_length - key_length - 1] == '/';
}

static bool ota_manifest_signature_key_id_is_valid(const char *key_id) {
    if (key_id == NULL || key_id[0] == '\0' ||
        strlen(key_id) >= OTA_MANIFEST_KEY_ID_SIZE) {
        return false;
    }
    for (const char *cursor = key_id; *cursor != '\0'; ++cursor) {
        const bool is_alphanumeric =
            (*cursor >= 'A' && *cursor <= 'Z') ||
            (*cursor >= 'a' && *cursor <= 'z') ||
            (*cursor >= '0' && *cursor <= '9');
        if (!is_alphanumeric &&
            *cursor != '.' && *cursor != '_' &&
            *cursor != ':' && *cursor != '-') {
            return false;
        }
    }
    return true;
}

static bool ota_manifest_time_is_valid(const char *value) {
    if (value == NULL || strlen(value) < 20) {
        return false;
    }
    return isdigit((unsigned char)value[0]) &&
        isdigit((unsigned char)value[1]) &&
        isdigit((unsigned char)value[2]) &&
        isdigit((unsigned char)value[3]) &&
        value[4] == '-' &&
        isdigit((unsigned char)value[5]) &&
        isdigit((unsigned char)value[6]) &&
        value[7] == '-' &&
        isdigit((unsigned char)value[8]) &&
        isdigit((unsigned char)value[9]) &&
        value[10] == 'T' &&
        isdigit((unsigned char)value[11]) &&
        isdigit((unsigned char)value[12]) &&
        value[13] == ':' &&
        isdigit((unsigned char)value[14]) &&
        isdigit((unsigned char)value[15]) &&
        value[16] == ':' &&
        isdigit((unsigned char)value[17]) &&
        isdigit((unsigned char)value[18]) &&
        value[19] == 'Z';
}

bool ota_manifest_sha256_is_valid(const char *sha256) {
    if (sha256 == NULL || strlen(sha256) != 64) {
        return false;
    }
    for (size_t index = 0; index < 64; ++index) {
        const char value = sha256[index];
        const bool digit = value >= '0' && value <= '9';
        const bool lower = value >= 'a' && value <= 'f';
        const bool upper = value >= 'A' && value <= 'F';
        if (!digit && !lower && !upper) {
            return false;
        }
    }
    return true;
}

bool ota_manifest_sha256_matches(
    const char *expected,
    const char *actual
) {
    if (!ota_manifest_sha256_is_valid(expected) ||
        !ota_manifest_sha256_is_valid(actual)) {
        return false;
    }
    for (size_t index = 0; index < 64; ++index) {
        if (tolower((unsigned char)expected[index]) !=
            tolower((unsigned char)actual[index])) {
            return false;
        }
    }
    return true;
}

static bool ota_manifest_next_version_component(
    const char **cursor,
    uint32_t *component_out
) {
    if (*cursor == NULL || **cursor == '\0' ||
        !isdigit((unsigned char)**cursor)) {
        return false;
    }
    uint32_t component = 0;
    while (isdigit((unsigned char)**cursor)) {
        const uint32_t digit = (uint32_t)(**cursor - '0');
        if (component > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        component = component * 10U + digit;
        ++*cursor;
    }
    *component_out = component;
    return true;
}

int ota_version_compare(const char *left, const char *right) {
    if (left == NULL || right == NULL) {
        return left == right ? 0 : (left == NULL ? -1 : 1);
    }
    const char *left_cursor = left;
    const char *right_cursor = right;
    for (int component = 0; component < 3; ++component) {
        uint32_t left_value = 0;
        uint32_t right_value = 0;
        if (!ota_manifest_next_version_component(
                &left_cursor,
                &left_value
            ) ||
            !ota_manifest_next_version_component(
                &right_cursor,
                &right_value
            )) {
            return strcmp(left, right);
        }
        if (left_value != right_value) {
            return left_value < right_value ? -1 : 1;
        }
        if (component < 2) {
            if (*left_cursor != '.' || *right_cursor != '.') {
                return strcmp(left, right);
            }
            ++left_cursor;
            ++right_cursor;
        }
    }
    // A pre-release suffix sorts before the same numeric release. Build
    // metadata does not change ordering, matching semantic version rules.
    const bool left_prerelease = *left_cursor == '-';
    const bool right_prerelease = *right_cursor == '-';
    if (left_prerelease != right_prerelease) {
        return left_prerelease ? -1 : 1;
    }
    if (left_prerelease) {
        const int prerelease_result = strcmp(left_cursor, right_cursor);
        if (prerelease_result != 0) {
            return prerelease_result < 0 ? -1 : 1;
        }
    }
    return 0;
}

ota_manifest_error_t ota_manifest_parse(
    const char *json,
    size_t json_size,
    ota_manifest_t *manifest_out
) {
    if (json == NULL || manifest_out == NULL || json_size == 0) {
        return OTA_MANIFEST_ERR_NULL_ARGUMENT;
    }
    memset(manifest_out, 0, sizeof(*manifest_out));
    ota_json_cursor_t cursor = {
        .cursor = json,
        .end = json + json_size,
        .error = OTA_MANIFEST_OK,
    };
    if (!ota_json_expect(&cursor, '{')) {
        return cursor.error;
    }

    bool seen_schema_version = false;
    bool seen_release_id = false;
    bool seen_firmware_version = false;
    bool seen_hardware_revision = false;
    bool seen_channel = false;
    bool seen_artifact_key = false;
    bool seen_artifact_url = false;
    bool seen_sha256 = false;
    bool seen_size_bytes = false;
    bool seen_signature_key_id = false;
    bool seen_signature_algorithm = false;
    bool seen_signature_payload_version = false;
    bool seen_signature = false;
    bool seen_rollback_allowed = false;
    bool seen_mandatory = false;
    bool seen_published_at = false;
    bool seen_min_source_version = false;

    ota_json_skip_whitespace(&cursor);
    if (cursor.cursor >= cursor.end) {
        return OTA_MANIFEST_ERR_JSON;
    }
    if (*cursor.cursor == '}') {
        return OTA_MANIFEST_ERR_MISSING_FIELD;
    }

    while (cursor.cursor < cursor.end) {
        char field[64] = {0};
        if (!ota_json_parse_string(&cursor, field, sizeof(field)) ||
            !ota_json_expect(&cursor, ':')) {
            return cursor.error;
        }

        bool *seen = NULL;
        if (strcmp(field, "schema_version") == 0) {
            seen = &seen_schema_version;
        } else if (strcmp(field, "release_id") == 0) {
            seen = &seen_release_id;
        } else if (strcmp(field, "firmware_version") == 0) {
            seen = &seen_firmware_version;
        } else if (strcmp(field, "hardware_revision") == 0) {
            seen = &seen_hardware_revision;
        } else if (strcmp(field, "channel") == 0) {
            seen = &seen_channel;
        } else if (strcmp(field, "artifact_key") == 0) {
            seen = &seen_artifact_key;
        } else if (strcmp(field, "artifact_url") == 0) {
            seen = &seen_artifact_url;
        } else if (strcmp(field, "sha256") == 0) {
            seen = &seen_sha256;
        } else if (strcmp(field, "size_bytes") == 0) {
            seen = &seen_size_bytes;
        } else if (strcmp(field, "signature_key_id") == 0) {
            seen = &seen_signature_key_id;
        } else if (strcmp(field, "signature_algorithm") == 0) {
            seen = &seen_signature_algorithm;
        } else if (strcmp(field, "signature_payload_version") == 0) {
            seen = &seen_signature_payload_version;
        } else if (strcmp(field, "signature") == 0) {
            seen = &seen_signature;
        } else if (strcmp(field, "rollback_allowed") == 0) {
            seen = &seen_rollback_allowed;
        } else if (strcmp(field, "mandatory") == 0) {
            seen = &seen_mandatory;
        } else if (strcmp(field, "published_at") == 0) {
            seen = &seen_published_at;
        } else if (strcmp(field, "min_source_version") == 0) {
            seen = &seen_min_source_version;
        } else {
            return OTA_MANIFEST_ERR_UNSUPPORTED_FIELD;
        }
        if (*seen) {
            return OTA_MANIFEST_ERR_DUPLICATE_FIELD;
        }
        *seen = true;

        if (seen == &seen_size_bytes) {
            if (!ota_json_parse_unsigned(&cursor, &manifest_out->size_bytes)) {
                return cursor.error;
            }
        } else if (seen == &seen_rollback_allowed ||
                   seen == &seen_mandatory) {
            bool *target = seen == &seen_rollback_allowed
                               ? &manifest_out->rollback_allowed
                               : &manifest_out->mandatory;
            if (!ota_json_parse_bool(
                    &cursor,
                    target
                )) {
                return cursor.error;
            }
        } else {
            char value[OTA_MANIFEST_URL_SIZE + 1] = {0};
            if (!ota_json_parse_string(&cursor, value, sizeof(value))) {
                return cursor.error;
            }
            size_t maximum_length = 0;
            ota_manifest_error_t length_error = OTA_MANIFEST_ERR_JSON;
            if (seen == &seen_schema_version) {
                maximum_length = OTA_MANIFEST_SCHEMA_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_SCHEMA_VERSION;
            } else if (seen == &seen_release_id) {
                maximum_length = OTA_MANIFEST_RELEASE_ID_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_RELEASE_ID;
            } else if (seen == &seen_firmware_version) {
                maximum_length = OTA_MANIFEST_VERSION_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_FIRMWARE_VERSION;
            } else if (seen == &seen_hardware_revision) {
                maximum_length = OTA_MANIFEST_HARDWARE_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_HARDWARE_REVISION;
            } else if (seen == &seen_channel) {
                maximum_length = OTA_MANIFEST_CHANNEL_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_CHANNEL;
            } else if (seen == &seen_artifact_key) {
                maximum_length = OTA_MANIFEST_ARTIFACT_KEY_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_ARTIFACT_KEY;
            } else if (seen == &seen_artifact_url) {
                maximum_length = OTA_MANIFEST_URL_MAX_BYTES;
                length_error = OTA_MANIFEST_ERR_ARTIFACT_URL;
            } else if (seen == &seen_sha256) {
                maximum_length = OTA_MANIFEST_HASH_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_SHA256;
            } else if (seen == &seen_signature_key_id) {
                maximum_length = OTA_MANIFEST_KEY_ID_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_SIGNATURE;
            } else if (seen == &seen_signature_algorithm) {
                maximum_length = OTA_MANIFEST_SIGNATURE_ALGORITHM_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_SIGNATURE;
            } else if (seen == &seen_signature_payload_version) {
                maximum_length = OTA_MANIFEST_PAYLOAD_VERSION_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_SIGNATURE_PAYLOAD_VERSION;
            } else if (seen == &seen_signature) {
                maximum_length = OTA_MANIFEST_SIGNATURE_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_SIGNATURE;
            } else if (seen == &seen_published_at) {
                maximum_length = OTA_MANIFEST_TIME_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_PUBLISHED_AT;
            } else if (seen == &seen_min_source_version) {
                maximum_length = OTA_MANIFEST_VERSION_SIZE - 1;
                length_error = OTA_MANIFEST_ERR_MIN_SOURCE_VERSION;
            }
            if (strlen(value) > maximum_length) {
                return length_error;
            }
            if (seen == &seen_schema_version) {
                (void)ota_manifest_copy_field(
                    manifest_out->schema_version,
                    sizeof(manifest_out->schema_version),
                    value
                );
            } else if (seen == &seen_release_id) {
                (void)ota_manifest_copy_field(
                    manifest_out->release_id,
                    sizeof(manifest_out->release_id),
                    value
                );
            } else if (seen == &seen_firmware_version) {
                (void)ota_manifest_copy_field(
                    manifest_out->firmware_version,
                    sizeof(manifest_out->firmware_version),
                    value
                );
            } else if (seen == &seen_hardware_revision) {
                (void)ota_manifest_copy_field(
                    manifest_out->hardware_revision,
                    sizeof(manifest_out->hardware_revision),
                    value
                );
            } else if (seen == &seen_channel) {
                (void)ota_manifest_copy_field(
                    manifest_out->channel,
                    sizeof(manifest_out->channel),
                    value
                );
            } else if (seen == &seen_artifact_key) {
                (void)ota_manifest_copy_field(
                    manifest_out->artifact_key,
                    sizeof(manifest_out->artifact_key),
                    value
                );
            } else if (seen == &seen_artifact_url) {
                (void)ota_manifest_copy_field(
                    manifest_out->artifact_url,
                    sizeof(manifest_out->artifact_url),
                    value
                );
            } else if (seen == &seen_sha256) {
                (void)ota_manifest_copy_field(
                    manifest_out->sha256,
                    sizeof(manifest_out->sha256),
                    value
                );
            } else if (seen == &seen_signature_key_id) {
                (void)ota_manifest_copy_field(
                    manifest_out->signature_key_id,
                    sizeof(manifest_out->signature_key_id),
                    value
                );
            } else if (seen == &seen_signature_algorithm) {
                (void)ota_manifest_copy_field(
                    manifest_out->signature_algorithm,
                    sizeof(manifest_out->signature_algorithm),
                    value
                );
            } else if (seen == &seen_signature_payload_version) {
                (void)ota_manifest_copy_field(
                    manifest_out->signature_payload_version,
                    sizeof(manifest_out->signature_payload_version),
                    value
                );
            } else if (seen == &seen_signature) {
                (void)ota_manifest_copy_field(
                    manifest_out->signature,
                    sizeof(manifest_out->signature),
                    value
                );
            } else if (seen == &seen_published_at) {
                (void)ota_manifest_copy_field(
                    manifest_out->published_at,
                    sizeof(manifest_out->published_at),
                    value
                );
            } else if (seen == &seen_min_source_version) {
                (void)ota_manifest_copy_field(
                    manifest_out->min_source_version,
                    sizeof(manifest_out->min_source_version),
                    value
                );
            }
        }

        ota_json_skip_whitespace(&cursor);
        if (cursor.cursor >= cursor.end) {
            return OTA_MANIFEST_ERR_JSON;
        }
        if (*cursor.cursor == '}') {
            ++cursor.cursor;
            break;
        }
        if (!ota_json_expect(&cursor, ',')) {
            return cursor.error;
        }
    }

    if (!seen_schema_version || !seen_release_id ||
        !seen_firmware_version || !seen_hardware_revision ||
        !seen_channel || !seen_artifact_key || !seen_artifact_url ||
        !seen_sha256 ||
        !seen_size_bytes || !seen_signature_key_id ||
        !seen_signature_algorithm || !seen_signature_payload_version ||
        !seen_signature || !seen_rollback_allowed || !seen_mandatory ||
        !seen_published_at ||
        !seen_min_source_version) {
        return OTA_MANIFEST_ERR_MISSING_FIELD;
    }
    ota_json_skip_whitespace(&cursor);
    if (cursor.cursor != cursor.end) {
        return OTA_MANIFEST_ERR_JSON;
    }

    if (strcmp(manifest_out->schema_version, OTA_MANIFEST_SCHEMA_VERSION) != 0) {
        return OTA_MANIFEST_ERR_SCHEMA_VERSION;
    }
    if (!ota_manifest_validate_string_field(
            manifest_out->release_id,
            OTA_MANIFEST_RELEASE_ID_SIZE - 1
        )) {
        return OTA_MANIFEST_ERR_RELEASE_ID;
    }
    if (!ota_manifest_semver_is_valid(manifest_out->firmware_version)) {
        return OTA_MANIFEST_ERR_FIRMWARE_VERSION;
    }
    if (!ota_manifest_validate_string_field(
            manifest_out->hardware_revision,
            OTA_MANIFEST_HARDWARE_SIZE - 1
        )) {
        return OTA_MANIFEST_ERR_HARDWARE_REVISION;
    }
    if (!ota_manifest_channel_is_valid(manifest_out->channel)) {
        return OTA_MANIFEST_ERR_CHANNEL;
    }
    if (!ota_manifest_artifact_key_is_valid(manifest_out->artifact_key)) {
        return OTA_MANIFEST_ERR_ARTIFACT_KEY;
    }
    if (!ota_manifest_url_is_https(manifest_out->artifact_url)) {
        return OTA_MANIFEST_ERR_ARTIFACT_URL;
    }
    if (!ota_manifest_artifact_key_matches_url(
            manifest_out->artifact_key,
            manifest_out->artifact_url
        )) {
        return OTA_MANIFEST_ERR_ARTIFACT_URL;
    }
    if (!ota_manifest_sha256_is_valid(manifest_out->sha256)) {
        return OTA_MANIFEST_ERR_SHA256;
    }
    if (manifest_out->size_bytes == 0 ||
        manifest_out->size_bytes >
            OTA_MANIFEST_MAX_ARTIFACT_SIZE_BYTES) {
        return OTA_MANIFEST_ERR_SIZE;
    }
    if (strcmp(
            manifest_out->signature_payload_version,
            OTA_MANIFEST_SIGNATURE_PAYLOAD_VERSION
        ) != 0) {
        return OTA_MANIFEST_ERR_SIGNATURE_PAYLOAD_VERSION;
    }
    if (!ota_manifest_signature_key_id_is_valid(
            manifest_out->signature_key_id
        ) ||
        !ota_manifest_signature_algorithm_is_ed25519(
            manifest_out->signature_algorithm
        ) ||
        !ota_manifest_validate_string_field(
            manifest_out->signature,
            OTA_MANIFEST_SIGNATURE_SIZE - 1
        )) {
        return OTA_MANIFEST_ERR_SIGNATURE;
    }
    if (!ota_manifest_time_is_valid(manifest_out->published_at)) {
        return OTA_MANIFEST_ERR_PUBLISHED_AT;
    }
    if (!ota_manifest_semver_is_valid(manifest_out->min_source_version)) {
        return OTA_MANIFEST_ERR_MIN_SOURCE_VERSION;
    }
    return OTA_MANIFEST_OK;
}

ota_manifest_error_t ota_manifest_validate_device(
    const ota_manifest_t *manifest,
    const char *current_firmware_version,
    const char *hardware_revision,
    const char *channel,
    bool allow_downgrade
) {
    if (manifest == NULL || current_firmware_version == NULL ||
        hardware_revision == NULL || channel == NULL) {
        return OTA_MANIFEST_ERR_NULL_ARGUMENT;
    }
    if (strcmp(manifest->hardware_revision, hardware_revision) != 0) {
        return OTA_MANIFEST_ERR_HARDWARE_REVISION;
    }
    if (strcmp(manifest->channel, channel) != 0) {
        return OTA_MANIFEST_ERR_CHANNEL;
    }
    if (ota_version_compare(
            current_firmware_version,
            manifest->min_source_version
        ) < 0) {
        return OTA_MANIFEST_ERR_VERSION_TOO_OLD;
    }
    const int version_order = ota_version_compare(
        manifest->firmware_version,
        current_firmware_version
    );
    if (version_order < 0 &&
        (!allow_downgrade || !manifest->rollback_allowed)) {
        return OTA_MANIFEST_ERR_ROLLBACK_NOT_ALLOWED;
    }
    if (version_order == 0) {
        return OTA_MANIFEST_ERR_VERSION_ORDER;
    }
    return OTA_MANIFEST_OK;
}

ota_manifest_error_t ota_manifest_from_platform_release(
    const ota_platform_release_t *release,
    const char *fallback_published_at,
    ota_manifest_t *manifest_out
) {
    if (release == NULL) {
        return OTA_MANIFEST_ERR_NULL_ARGUMENT;
    }
    if (manifest_out == NULL) {
        return OTA_MANIFEST_ERR_NULL_ARGUMENT;
    }
    memset(manifest_out, 0, sizeof(*manifest_out));
    if (!ota_manifest_copy_field(
            manifest_out->schema_version,
            sizeof(manifest_out->schema_version),
            OTA_MANIFEST_SCHEMA_VERSION
        ) ||
        !ota_manifest_copy_field(
            manifest_out->release_id,
            sizeof(manifest_out->release_id),
            release->release_id
        ) ||
        !ota_manifest_copy_field(
            manifest_out->firmware_version,
            sizeof(manifest_out->firmware_version),
            release->version
        ) ||
        !ota_manifest_copy_field(
            manifest_out->hardware_revision,
            sizeof(manifest_out->hardware_revision),
            release->hardware_revision
        ) ||
        !ota_manifest_copy_field(
            manifest_out->channel,
            sizeof(manifest_out->channel),
            release->channel
        ) ||
        !ota_manifest_copy_field(
            manifest_out->artifact_key,
            sizeof(manifest_out->artifact_key),
            release->artifact_key
        ) ||
        !ota_manifest_copy_field(
            manifest_out->artifact_url,
            sizeof(manifest_out->artifact_url),
            release->artifact_url
        ) ||
        !ota_manifest_copy_field(
            manifest_out->sha256,
            sizeof(manifest_out->sha256),
            release->sha256
        ) ||
        !ota_manifest_copy_field(
            manifest_out->signature_key_id,
            sizeof(manifest_out->signature_key_id),
            release->signature_key_id
        ) ||
        !ota_manifest_copy_field(
            manifest_out->signature_algorithm,
            sizeof(manifest_out->signature_algorithm),
            release->signature_algorithm
        ) ||
        !ota_manifest_copy_field(
            manifest_out->signature_payload_version,
            sizeof(manifest_out->signature_payload_version),
            release->signature_payload_version
        )) {
        return OTA_MANIFEST_ERR_MISSING_FIELD;
    }
    const char *firmware_version = manifest_out->firmware_version;
    if (!ota_manifest_semver_is_valid(firmware_version)) {
        return OTA_MANIFEST_ERR_FIRMWARE_VERSION;
    }
    if (!ota_manifest_signature_key_id_is_valid(
            manifest_out->signature_key_id
        )) {
        return OTA_MANIFEST_ERR_MISSING_FIELD;
    }
    if (!ota_manifest_signature_algorithm_is_ed25519(
            manifest_out->signature_algorithm
        )) {
        return OTA_MANIFEST_ERR_SIGNATURE;
    }
    if (!ota_manifest_channel_is_valid(manifest_out->channel)) {
        return OTA_MANIFEST_ERR_CHANNEL;
    }
    if (!ota_manifest_artifact_key_is_valid(manifest_out->artifact_key)) {
        return OTA_MANIFEST_ERR_ARTIFACT_KEY;
    }
    if (!ota_manifest_url_is_https(manifest_out->artifact_url)) {
        return OTA_MANIFEST_ERR_ARTIFACT_URL;
    }
    if (!ota_manifest_artifact_key_matches_url(
            manifest_out->artifact_key,
            manifest_out->artifact_url
        )) {
        return OTA_MANIFEST_ERR_ARTIFACT_URL;
    }
    if (!ota_manifest_sha256_is_valid(manifest_out->sha256)) {
        return OTA_MANIFEST_ERR_SHA256;
    }
    if (strcmp(
            manifest_out->signature_payload_version,
            OTA_MANIFEST_SIGNATURE_PAYLOAD_VERSION
        ) != 0) {
        return OTA_MANIFEST_ERR_SIGNATURE_PAYLOAD_VERSION;
    }
    manifest_out->size_bytes = release->size_bytes;
    if (manifest_out->size_bytes == 0 ||
        manifest_out->size_bytes >
            OTA_MANIFEST_MAX_ARTIFACT_SIZE_BYTES) {
        return OTA_MANIFEST_ERR_SIZE;
    }
    manifest_out->rollback_allowed = release->rollback_allowed;
    manifest_out->mandatory = release->mandatory;

    const char *min_source_version = release->min_source_version;
    if (min_source_version == NULL || min_source_version[0] == '\0') {
        return OTA_MANIFEST_ERR_MIN_SOURCE_VERSION;
    }
    if (!ota_manifest_semver_is_valid(min_source_version) ||
        !ota_manifest_copy_field(
            manifest_out->min_source_version,
            sizeof(manifest_out->min_source_version),
            min_source_version
        )) {
        return OTA_MANIFEST_ERR_MIN_SOURCE_VERSION;
    }

    const char *published_at =
        release->published_at != NULL && release->published_at[0] != '\0'
            ? release->published_at
            : fallback_published_at;
    if (!ota_manifest_time_is_valid(published_at) ||
        !ota_manifest_copy_field(
            manifest_out->published_at,
            sizeof(manifest_out->published_at),
            published_at
        )) {
        return OTA_MANIFEST_ERR_PUBLISHED_AT;
    }

    // A detached signature over the artifact digest is a required security
    // field. It must never be substituted with the artifact hash: doing so
    // would fake a cryptographic attestation the device cannot verify.
    if (!ota_manifest_validate_string_field(
            release->signature,
            OTA_MANIFEST_SIGNATURE_SIZE - 1
        ) ||
        !ota_manifest_copy_field(
            manifest_out->signature,
            sizeof(manifest_out->signature),
            release->signature
        )) {
        return OTA_MANIFEST_ERR_SIGNATURE;
    }
    return OTA_MANIFEST_OK;
}

static bool ota_manifest_json_read_string(
    const char **cursor_out,
    char *output,
    size_t output_size
) {
    if (cursor_out == NULL || *cursor_out == NULL || output == NULL ||
        output_size == 0) {
        return false;
    }
    const char *cursor = *cursor_out + 1;
    size_t output_length = 0;
    while (*cursor != '\0') {
        const unsigned char value = (unsigned char)*cursor;
        ++cursor;
        if (value == '"') {
            output[output_length] = '\0';
            *cursor_out = cursor;
            return true;
        }
        if (value < 0x20 || value == '\\' ||
            output_length + 1 >= output_size) {
            return false;
        }
        output[output_length++] = (char)value;
    }
    return false;
}

static void ota_manifest_json_skip_whitespace(const char **cursor) {
    while (**cursor == ' ' || **cursor == '\t' ||
           **cursor == '\r' || **cursor == '\n') {
        ++*cursor;
    }
}

static bool ota_manifest_keyring_parse(
    const char *json,
    const char *target_key_id,
    ota_manifest_keyring_entry_t *selected_out
) {
    if (json == NULL || target_key_id == NULL || selected_out == NULL) {
        return false;
    }
    memset(selected_out, 0, sizeof(*selected_out));
    bool target_found = false;
    const char *cursor = json;
    ota_manifest_json_skip_whitespace(&cursor);
    if (*cursor++ != '{') {
        return false;
    }
    ota_manifest_json_skip_whitespace(&cursor);
    if (*cursor == '}') {
        ++cursor;
        ota_manifest_json_skip_whitespace(&cursor);
        return *cursor == '\0';
    }
    while (*cursor != '\0') {
        ota_manifest_keyring_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        if (*cursor != '"' ||
            !ota_manifest_json_read_string(
                &cursor,
                entry.key_id,
                sizeof(entry.key_id)
            )) {
            return false;
        }
        if (!ota_manifest_signature_key_id_is_valid(entry.key_id)) {
            return false;
        }
        ota_manifest_json_skip_whitespace(&cursor);
        if (*cursor++ != ':') {
            return false;
        }
        ota_manifest_json_skip_whitespace(&cursor);
        if (*cursor != '"' ||
            !ota_manifest_json_read_string(
                &cursor,
                entry.public_key,
                sizeof(entry.public_key)
            )) {
            return false;
        }
        uint8_t decoded[OTA_MANIFEST_BASE64_PUBLIC_KEY_SIZE] = {0};
        size_t decoded_size = 0;
        const bool public_key_is_valid =
            ota_manifest_base64_decode(
                entry.public_key,
                decoded,
                sizeof(decoded),
                &decoded_size
            ) &&
            decoded_size == sizeof(decoded) &&
            ota_manifest_canonical_base64(
                entry.public_key,
                decoded,
                decoded_size
            );
        memset(decoded, 0, sizeof(decoded));
        if (!public_key_is_valid) {
            return false;
        }
        if (strcmp(entry.key_id, target_key_id) == 0) {
            if (target_found) {
                return false;
            }
            target_found = true;
            *selected_out = entry;
        }
        ota_manifest_json_skip_whitespace(&cursor);
        if (*cursor == '}') {
            ++cursor;
            ota_manifest_json_skip_whitespace(&cursor);
            return *cursor == '\0' && target_found;
        }
        if (*cursor++ != ',') {
            return false;
        }
        ota_manifest_json_skip_whitespace(&cursor);
    }
    return false;
}

ota_manifest_error_t ota_manifest_verify_signature(
    const ota_manifest_t *manifest,
    const char *trusted_keyring_json
) {
    if (manifest == NULL || trusted_keyring_json == NULL) {
        return OTA_MANIFEST_ERR_NULL_ARGUMENT;
    }
    if (!ota_manifest_signature_algorithm_is_ed25519(
            manifest->signature_algorithm
        ) ||
        !ota_manifest_signature_key_id_is_valid(
            manifest->signature_key_id
        )) {
        return OTA_MANIFEST_ERR_SIGNATURE;
    }
    if (strcmp(
            manifest->signature_payload_version,
            OTA_MANIFEST_SIGNATURE_PAYLOAD_VERSION
        ) != 0) {
        return OTA_MANIFEST_ERR_SIGNATURE_PAYLOAD_VERSION;
    }
    if (!ota_manifest_artifact_key_is_valid(manifest->artifact_key) ||
        !ota_manifest_url_is_https(manifest->artifact_url) ||
        !ota_manifest_artifact_key_matches_url(
            manifest->artifact_key,
            manifest->artifact_url
        )) {
        return OTA_MANIFEST_ERR_SIGNATURE;
    }
    if (!ota_manifest_sha256_is_valid(manifest->sha256)) {
        return OTA_MANIFEST_ERR_SIGNATURE;
    }
    ota_manifest_keyring_entry_t trusted_key;
    memset(&trusted_key, 0, sizeof(trusted_key));
    const bool trusted_key_is_valid = ota_manifest_keyring_parse(
        trusted_keyring_json,
        manifest->signature_key_id,
        &trusted_key
    );
    if (!trusted_key_is_valid) {
        memset(&trusted_key, 0, sizeof(trusted_key));
        return OTA_MANIFEST_ERR_SIGNATURE;
    }
    uint8_t public_key[OTA_MANIFEST_BASE64_PUBLIC_KEY_SIZE];
    memset(public_key, 0, sizeof(public_key));
    size_t public_key_size = 0;
    const bool public_key_is_valid =
        ota_manifest_base64_decode(
            trusted_key.public_key,
            public_key,
            sizeof(public_key),
            &public_key_size
        ) &&
        public_key_size == sizeof(public_key) &&
        ota_manifest_canonical_base64(
            trusted_key.public_key,
            public_key,
            public_key_size
        );
    if (!public_key_is_valid) {
        memset(&trusted_key, 0, sizeof(trusted_key));
        return OTA_MANIFEST_ERR_SIGNATURE;
    }
    uint8_t signature[OTA_MANIFEST_BASE64_DECODED_SIGNATURE_SIZE];
    memset(signature, 0, sizeof(signature));
    size_t signature_size = 0;
    if (!ota_manifest_base64_decode(
            manifest->signature,
            signature,
            sizeof(signature),
            &signature_size
        ) ||
        signature_size != sizeof(signature) ||
        !ota_manifest_canonical_base64(
            manifest->signature,
            signature,
            signature_size
        )) {
        memset(&trusted_key, 0, sizeof(trusted_key));
        return OTA_MANIFEST_ERR_SIGNATURE;
    }
    char canonical_payload[OTA_MANIFEST_CANONICAL_PAYLOAD_SIZE];
    memset(canonical_payload, 0, sizeof(canonical_payload));
    size_t canonical_payload_length = 0;
    if (!ota_manifest_build_signature_payload(
            manifest,
            canonical_payload,
            sizeof(canonical_payload),
            &canonical_payload_length
        )) {
        memset(public_key, 0, sizeof(public_key));
        memset(signature, 0, sizeof(signature));
        memset(canonical_payload, 0, sizeof(canonical_payload));
        memset(&trusted_key, 0, sizeof(trusted_key));
        return OTA_MANIFEST_ERR_SIGNATURE;
    }
    const bool verified = ed25519_verify(
        public_key,
        signature,
        (const uint8_t *)canonical_payload,
        canonical_payload_length
    );
    memset(public_key, 0, sizeof(public_key));
    memset(signature, 0, sizeof(signature));
    memset(canonical_payload, 0, sizeof(canonical_payload));
    memset(&trusted_key, 0, sizeof(trusted_key));
    return verified ? OTA_MANIFEST_OK : OTA_MANIFEST_ERR_SIGNATURE;
}

const char *ota_manifest_error_name(ota_manifest_error_t error) {
    switch (error) {
        case OTA_MANIFEST_OK:
            return "ok";
        case OTA_MANIFEST_ERR_NULL_ARGUMENT:
            return "null_argument";
        case OTA_MANIFEST_ERR_JSON:
            return "invalid_json";
        case OTA_MANIFEST_ERR_SCHEMA_VERSION:
            return "unsupported_schema";
        case OTA_MANIFEST_ERR_RELEASE_ID:
            return "invalid_release_id";
        case OTA_MANIFEST_ERR_FIRMWARE_VERSION:
            return "invalid_firmware_version";
        case OTA_MANIFEST_ERR_HARDWARE_REVISION:
            return "hardware_mismatch";
        case OTA_MANIFEST_ERR_CHANNEL:
            return "channel_mismatch";
        case OTA_MANIFEST_ERR_ARTIFACT_KEY:
            return "invalid_artifact_key";
        case OTA_MANIFEST_ERR_ARTIFACT_URL:
            return "invalid_artifact_url";
        case OTA_MANIFEST_ERR_SHA256:
            return "invalid_sha256";
        case OTA_MANIFEST_ERR_SIZE:
            return "invalid_size";
        case OTA_MANIFEST_ERR_SIGNATURE:
            return "invalid_signature";
        case OTA_MANIFEST_ERR_SIGNATURE_PAYLOAD_VERSION:
            return "unsupported_signature_payload";
        case OTA_MANIFEST_ERR_PUBLISHED_AT:
            return "invalid_published_at";
        case OTA_MANIFEST_ERR_MIN_SOURCE_VERSION:
            return "invalid_min_source_version";
        case OTA_MANIFEST_ERR_DUPLICATE_FIELD:
            return "duplicate_field";
        case OTA_MANIFEST_ERR_MISSING_FIELD:
            return "missing_field";
        case OTA_MANIFEST_ERR_UNSUPPORTED_FIELD:
            return "unsupported_field";
        case OTA_MANIFEST_ERR_VERSION_ORDER:
            return "version_not_newer";
        case OTA_MANIFEST_ERR_VERSION_TOO_OLD:
            return "source_version_too_old";
        case OTA_MANIFEST_ERR_ROLLBACK_NOT_ALLOWED:
            return "downgrade_not_allowed";
        default:
            return "unknown";
    }
}
