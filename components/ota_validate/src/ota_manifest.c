#include "ota_manifest.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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

static bool ota_manifest_channel_is_valid(const char *channel) {
    return strcmp(channel, "stable") == 0 ||
        strcmp(channel, "canary") == 0 ||
        strcmp(channel, "internal") == 0;
}

static bool ota_manifest_url_is_https(const char *url) {
    return url != NULL && strncmp(url, "https://", 8) == 0 &&
        strchr(url + 8, '/') != NULL;
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
    bool seen_artifact_url = false;
    bool seen_sha256 = false;
    bool seen_size_bytes = false;
    bool seen_signature_key_id = false;
    bool seen_signature = false;
    bool seen_rollback_allowed = false;
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
        } else if (strcmp(field, "artifact_url") == 0) {
            seen = &seen_artifact_url;
        } else if (strcmp(field, "sha256") == 0) {
            seen = &seen_sha256;
        } else if (strcmp(field, "size_bytes") == 0) {
            seen = &seen_size_bytes;
        } else if (strcmp(field, "signature_key_id") == 0) {
            seen = &seen_signature_key_id;
        } else if (strcmp(field, "signature") == 0) {
            seen = &seen_signature;
        } else if (strcmp(field, "rollback_allowed") == 0) {
            seen = &seen_rollback_allowed;
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
        } else if (seen == &seen_rollback_allowed) {
            if (!ota_json_parse_bool(
                    &cursor,
                    &manifest_out->rollback_allowed
                )) {
                return cursor.error;
            }
        } else {
            char value[OTA_MANIFEST_SIGNATURE_SIZE] = {0};
            if (!ota_json_parse_string(&cursor, value, sizeof(value))) {
                return cursor.error;
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
        !seen_channel || !seen_artifact_url || !seen_sha256 ||
        !seen_size_bytes || !seen_signature_key_id || !seen_signature ||
        !seen_rollback_allowed || !seen_published_at ||
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
    if (!ota_manifest_url_is_https(manifest_out->artifact_url)) {
        return OTA_MANIFEST_ERR_ARTIFACT_URL;
    }
    if (!ota_manifest_sha256_is_valid(manifest_out->sha256)) {
        return OTA_MANIFEST_ERR_SHA256;
    }
    if (manifest_out->size_bytes == 0) {
        return OTA_MANIFEST_ERR_SIZE;
    }
    if (!ota_manifest_validate_string_field(
            manifest_out->signature_key_id,
            OTA_MANIFEST_KEY_ID_SIZE - 1
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
    if (release == NULL || manifest_out == NULL) {
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
        )) {
        return OTA_MANIFEST_ERR_MISSING_FIELD;
    }
    const char *firmware_version = manifest_out->firmware_version;
    if (!ota_manifest_semver_is_valid(firmware_version)) {
        return OTA_MANIFEST_ERR_FIRMWARE_VERSION;
    }
    if (!ota_manifest_validate_string_field(
            manifest_out->signature_key_id,
            OTA_MANIFEST_KEY_ID_SIZE - 1
        )) {
        return OTA_MANIFEST_ERR_MISSING_FIELD;
    }
    if (!ota_manifest_channel_is_valid(manifest_out->channel)) {
        return OTA_MANIFEST_ERR_CHANNEL;
    }
    if (!ota_manifest_url_is_https(manifest_out->artifact_url)) {
        return OTA_MANIFEST_ERR_ARTIFACT_URL;
    }
    if (!ota_manifest_sha256_is_valid(manifest_out->sha256)) {
        return OTA_MANIFEST_ERR_SHA256;
    }
    manifest_out->size_bytes = release->size_bytes;
    if (manifest_out->size_bytes == 0) {
        return OTA_MANIFEST_ERR_SIZE;
    }
    manifest_out->rollback_allowed = release->rollback_allowed;

    const char *min_source_version =
        release->min_source_version != NULL &&
                release->min_source_version[0] != '\0'
            ? release->min_source_version
            : "0.0.0";
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
        case OTA_MANIFEST_ERR_ARTIFACT_URL:
            return "invalid_artifact_url";
        case OTA_MANIFEST_ERR_SHA256:
            return "invalid_sha256";
        case OTA_MANIFEST_ERR_SIZE:
            return "invalid_size";
        case OTA_MANIFEST_ERR_SIGNATURE:
            return "invalid_signature";
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
