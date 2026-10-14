#include "device_mqtt.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *schema_version;
    size_t schema_version_length;
    const char *event_type;
    size_t event_type_length;
    const char *subject_type;
    size_t subject_type_length;
    const char *subject_id;
    size_t subject_id_length;
    const char *request_id;
    size_t request_id_length;
    const char *command_id;
    size_t command_id_length;
    const char *command_type;
    size_t command_type_length;
    const char *payload_request_id;
    size_t payload_request_id_length;
    const char *data;
    size_t data_length;
} device_mqtt_envelope_fields_t;

typedef struct {
    device_mqtt_command_record_t records[DEVICE_MQTT_DEDUPE_CAPACITY];
    size_t count;
    size_t next;
} device_mqtt_ledger_t;

static device_mqtt_ledger_t device_mqtt_ledger;

static void device_mqtt_skip_whitespace(const char **cursor) {
    while (**cursor != '\0' && isspace((unsigned char)**cursor)) {
        *cursor += 1;
    }
}

static bool device_mqtt_read_json_string(
    const char **cursor,
    const char **value_out,
    size_t *length_out
) {
    if (**cursor != '"') {
        return false;
    }
    const char *value = *cursor + 1;
    const char *current = value;
    while (*current != '\0' && *current != '"') {
        const unsigned char byte = (unsigned char)*current;
        if (byte < 0x20 || byte == '\\') {
            return false;
        }
        current += 1;
    }
    if (*current != '"') {
        return false;
    }
    *value_out = value;
    *length_out = (size_t)(current - value);
    *cursor = current + 1;
    return true;
}

static bool device_mqtt_read_key(
    const char **cursor,
    const char **key_out,
    size_t *key_length_out
) {
    if (!device_mqtt_read_json_string(cursor, key_out, key_length_out)) {
        return false;
    }
    device_mqtt_skip_whitespace(cursor);
    if (**cursor != ':') {
        return false;
    }
    *cursor += 1;
    device_mqtt_skip_whitespace(cursor);
    return true;
}

static bool device_mqtt_read_string_value(
    const char **cursor,
    const char **value_out,
    size_t *length_out
) {
    return device_mqtt_read_json_string(cursor, value_out, length_out);
}

static bool device_mqtt_skip_json_value(const char **cursor);

static bool device_mqtt_skip_json_object(const char **cursor) {
    if (**cursor != '{') {
        return false;
    }
    int depth = 0;
    bool in_string = false;
    const char *current = *cursor;
    while (*current != '\0') {
        const char character = *current;
        if (in_string) {
            if (character == '\\') {
                current += 1;
                if (*current == '\0') {
                    return false;
                }
            } else if (character == '"') {
                in_string = false;
            }
        } else if (character == '"') {
            in_string = true;
        } else if (character == '{') {
            depth += 1;
        } else if (character == '}') {
            depth -= 1;
            if (depth == 0) {
                *cursor = current + 1;
                return true;
            }
        }
        current += 1;
    }
    return false;
}

static bool device_mqtt_skip_json_array(const char **cursor) {
    if (**cursor != '[') {
        return false;
    }
    *cursor += 1;
    device_mqtt_skip_whitespace(cursor);
    if (**cursor == ']') {
        *cursor += 1;
        return true;
    }
    while (**cursor != '\0') {
        if (!device_mqtt_skip_json_value(cursor)) {
            return false;
        }
        device_mqtt_skip_whitespace(cursor);
        if (**cursor == ',') {
            *cursor += 1;
            device_mqtt_skip_whitespace(cursor);
            continue;
        }
        if (**cursor == ']') {
            *cursor += 1;
            return true;
        }
        return false;
    }
    return false;
}

static bool device_mqtt_skip_json_literal(
    const char **cursor,
    const char *literal
) {
    const size_t length = strlen(literal);
    if (strncmp(*cursor, literal, length) != 0) {
        return false;
    }
    *cursor += length;
    return true;
}

static bool device_mqtt_skip_json_number(const char **cursor) {
    const char *current = *cursor;
    if (*current == '-') {
        current += 1;
    }
    if (*current < '0' || *current > '9') {
        return false;
    }
    if (*current == '0') {
        current += 1;
    } else {
        while (*current >= '0' && *current <= '9') {
            current += 1;
        }
    }
    if (*current == '.') {
        current += 1;
        if (*current < '0' || *current > '9') {
            return false;
        }
        while (*current >= '0' && *current <= '9') {
            current += 1;
        }
    }
    if (*current == 'e' || *current == 'E') {
        current += 1;
        if (*current == '+' || *current == '-') {
            current += 1;
        }
        if (*current < '0' || *current > '9') {
            return false;
        }
        while (*current >= '0' && *current <= '9') {
            current += 1;
        }
    }
    *cursor = current;
    return true;
}

static bool device_mqtt_skip_json_value(const char **cursor) {
    switch (**cursor) {
        case '"': {
            const char *value = NULL;
            size_t length = 0;
            return device_mqtt_read_json_string(cursor, &value, &length);
        }
        case '{':
            return device_mqtt_skip_json_object(cursor);
        case '[':
            return device_mqtt_skip_json_array(cursor);
        case 't':
            return device_mqtt_skip_json_literal(cursor, "true");
        case 'f':
            return device_mqtt_skip_json_literal(cursor, "false");
        case 'n':
            return device_mqtt_skip_json_literal(cursor, "null");
        default:
            return device_mqtt_skip_json_number(cursor);
    }
}

static bool device_mqtt_read_hex4(const char **cursor, uint32_t *value_out) {
    uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        const unsigned char current = (unsigned char)**cursor;
        uint32_t digit = 0;
        if (current >= '0' && current <= '9') {
            digit = (uint32_t)(current - '0');
        } else if (current >= 'a' && current <= 'f') {
            digit = (uint32_t)(current - 'a') + 10;
        } else if (current >= 'A' && current <= 'F') {
            digit = (uint32_t)(current - 'A') + 10;
        } else {
            return false;
        }
        value = (value << 4) | digit;
        *cursor += 1;
    }
    *value_out = value;
    return true;
}

static bool device_mqtt_append_utf8(
    uint32_t codepoint,
    char *destination,
    size_t destination_size,
    size_t *length_out
) {
    size_t length = 0;
    if (codepoint <= 0x7f) {
        length = 1;
    } else if (codepoint <= 0x7ff) {
        length = 2;
    } else if (codepoint <= 0xffff) {
        if (codepoint >= 0xd800 && codepoint <= 0xdfff) {
            return false;
        }
        length = 3;
    } else if (codepoint <= 0x10ffff) {
        length = 4;
    } else {
        return false;
    }
    if (destination == NULL || *length_out > destination_size ||
        length > destination_size - *length_out - 1) {
        return false;
    }
    char *output = destination + *length_out;
    switch (length) {
        case 1:
            output[0] = (char)codepoint;
            break;
        case 2:
            output[0] = (char)(0xc0 | (codepoint >> 6));
            output[1] = (char)(0x80 | (codepoint & 0x3f));
            break;
        case 3:
            output[0] = (char)(0xe0 | (codepoint >> 12));
            output[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
            output[2] = (char)(0x80 | (codepoint & 0x3f));
            break;
        case 4:
            output[0] = (char)(0xf0 | (codepoint >> 18));
            output[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
            output[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
            output[3] = (char)(0x80 | (codepoint & 0x3f));
            break;
        default:
            return false;
    }
    *length_out += length;
    return true;
}

static bool device_mqtt_read_decoded_json_string(
    const char **cursor,
    char *destination,
    size_t destination_size
) {
    if (**cursor != '"' || destination == NULL || destination_size == 0) {
        return false;
    }
    const char *current = *cursor + 1;
    size_t length = 0;
    while (*current != '\0' && *current != '"') {
        const unsigned char byte = (unsigned char)*current;
        if (byte < 0x20) {
            return false;
        }
        if (byte != '\\') {
            if (length >= destination_size - 1) {
                return false;
            }
            destination[length++] = (char)byte;
            current += 1;
            continue;
        }
        current += 1;
        switch (*current) {
            case '"':
            case '\\':
            case '/':
                if (length >= destination_size - 1) {
                    return false;
                }
                destination[length++] = *current;
                current += 1;
                break;
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't': {
                if (length >= destination_size - 1) {
                    return false;
                }
                const char escaped =
                    *current == 'b'
                        ? '\b'
                        : *current == 'f'
                            ? '\f'
                            : *current == 'n'
                                ? '\n'
                                : *current == 'r' ? '\r' : '\t';
                destination[length++] = escaped;
                current += 1;
                break;
            }
            case 'u': {
                current += 1;
                uint32_t codepoint = 0;
                if (!device_mqtt_read_hex4(&current, &codepoint)) {
                    return false;
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (current[0] != '\\' || current[1] != 'u') {
                        return false;
                    }
                    current += 2;
                    uint32_t low_surrogate = 0;
                    if (!device_mqtt_read_hex4(
                            &current,
                            &low_surrogate
                        ) ||
                        low_surrogate < 0xdc00 ||
                        low_surrogate > 0xdfff) {
                        return false;
                    }
                    codepoint = 0x10000 +
                        ((codepoint - 0xd800) << 10) +
                        (low_surrogate - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return false;
                }
                if (!device_mqtt_append_utf8(
                        codepoint,
                        destination,
                        destination_size,
                        &length
                    )) {
                    return false;
                }
                break;
            }
            default:
                return false;
        }
    }
    if (*current != '"' || length >= destination_size) {
        return false;
    }
    destination[length] = '\0';
    *cursor = current + 1;
    return true;
}

static bool device_mqtt_read_bool_value(const char **cursor, bool *value_out) {
    if (value_out == NULL) {
        return false;
    }
    if (strncmp(*cursor, "true", 4) == 0) {
        *value_out = true;
        *cursor += 4;
        return true;
    }
    if (strncmp(*cursor, "false", 5) == 0) {
        *value_out = false;
        *cursor += 5;
        return true;
    }
    return false;
}

static bool device_mqtt_read_u32_value(const char **cursor, uint32_t *value_out) {
    if (value_out == NULL || **cursor < '0' || **cursor > '9') {
        return false;
    }
    uint32_t value = 0;
    const char *current = *cursor;
    while (*current >= '0' && *current <= '9') {
        const uint32_t digit = (uint32_t)(*current - '0');
        if (value > (UINT32_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        current += 1;
    }
    *value_out = value;
    *cursor = current;
    return true;
}

static bool device_mqtt_parse_payload(
    const char **cursor,
    device_mqtt_envelope_fields_t *fields
) {
    if (**cursor != '{') {
        return false;
    }
    *cursor += 1;
    device_mqtt_skip_whitespace(cursor);
    if (**cursor == '}') {
        *cursor += 1;
        return true;
    }
    while (**cursor != '\0') {
        const char *key = NULL;
        size_t key_length = 0;
        if (!device_mqtt_read_key(cursor, &key, &key_length)) {
            return false;
        }
        if (key_length == 10 && memcmp(key, "command_id", 10) == 0) {
            if (!device_mqtt_read_string_value(
                    cursor,
                    &fields->command_id,
                    &fields->command_id_length
                )) {
                return false;
            }
        } else if (key_length == 12 &&
                   memcmp(key, "command_type", 12) == 0) {
            if (!device_mqtt_read_string_value(
                    cursor,
                    &fields->command_type,
                    &fields->command_type_length
                )) {
                return false;
            }
        } else if (key_length == 10 &&
                   memcmp(key, "request_id", 10) == 0) {
            if (!device_mqtt_read_string_value(
                    cursor,
                    &fields->payload_request_id,
                    &fields->payload_request_id_length
                )) {
                return false;
            }
        } else if (key_length == 4 && memcmp(key, "data", 4) == 0) {
            // Keep the verbatim object: the command envelope is
            // type-agnostic and the executing module parses the fields its
            // command type defines.
            if (**cursor != '{') {
                if (!device_mqtt_skip_json_value(cursor)) {
                    return false;
                }
            } else {
                const char *data_start = *cursor;
                if (!device_mqtt_skip_json_object(cursor)) {
                    return false;
                }
                if (fields->data != NULL) {
                    return false;
                }
                fields->data = data_start;
                fields->data_length =
                    (size_t)(*cursor - data_start);
            }
        } else {
            // Unknown payload keys are skipped so their nested keys never
            // leak into envelope field matching.
            if (!device_mqtt_skip_json_value(cursor)) {
                return false;
            }
        }
        device_mqtt_skip_whitespace(cursor);
        if (**cursor == ',') {
            *cursor += 1;
            device_mqtt_skip_whitespace(cursor);
            continue;
        }
        if (**cursor == '}') {
            *cursor += 1;
            return true;
        }
        return false;
    }
    return false;
}

static bool device_mqtt_parse_subject(
    const char **cursor,
    device_mqtt_envelope_fields_t *fields
) {
    if (**cursor != '{') {
        return false;
    }
    *cursor += 1;
    device_mqtt_skip_whitespace(cursor);
    if (**cursor == '}') {
        *cursor += 1;
        return true;
    }
    while (**cursor != '\0') {
        const char *key = NULL;
        size_t key_length = 0;
        if (!device_mqtt_read_key(cursor, &key, &key_length)) {
            return false;
        }
        if (key_length == 4 && memcmp(key, "type", 4) == 0) {
            if (!device_mqtt_read_string_value(
                    cursor,
                    &fields->subject_type,
                    &fields->subject_type_length
                )) {
                return false;
            }
        } else if (key_length == 2 && memcmp(key, "id", 2) == 0) {
            if (!device_mqtt_read_string_value(
                    cursor,
                    &fields->subject_id,
                    &fields->subject_id_length
                )) {
                return false;
            }
        } else if (!device_mqtt_skip_json_value(cursor)) {
            return false;
        }
        device_mqtt_skip_whitespace(cursor);
        if (**cursor == ',') {
            *cursor += 1;
            device_mqtt_skip_whitespace(cursor);
            continue;
        }
        if (**cursor == '}') {
            *cursor += 1;
            return true;
        }
        return false;
    }
    return false;
}

static bool device_mqtt_parse_envelope(
    const char **cursor,
    device_mqtt_envelope_fields_t *fields
) {
    if (**cursor != '{') {
        return false;
    }
    *cursor += 1;
    device_mqtt_skip_whitespace(cursor);
    if (**cursor == '}') {
        *cursor += 1;
        return true;
    }
    while (**cursor != '\0') {
        const char *key = NULL;
        size_t key_length = 0;
        if (!device_mqtt_read_key(cursor, &key, &key_length)) {
            return false;
        }
        const bool is_schema_version =
            key_length == 14 && memcmp(key, "schema_version", 14) == 0;
        const bool is_event_type =
            key_length == 10 && memcmp(key, "event_type", 10) == 0;
        const bool is_request_id =
            key_length == 10 && memcmp(key, "request_id", 10) == 0;
        const bool is_subject =
            key_length == 7 && memcmp(key, "subject", 7) == 0;
        const bool is_payload =
            key_length == 7 && memcmp(key, "payload", 7) == 0;
        if (is_schema_version) {
            if (!device_mqtt_read_string_value(
                    cursor,
                    &fields->schema_version,
                    &fields->schema_version_length
                )) {
                return false;
            }
        } else if (is_event_type) {
            if (!device_mqtt_read_string_value(
                    cursor,
                    &fields->event_type,
                    &fields->event_type_length
                )) {
                return false;
            }
        } else if (is_request_id) {
            if (!device_mqtt_read_string_value(
                    cursor,
                    &fields->request_id,
                    &fields->request_id_length
                )) {
                return false;
            }
        } else if (is_subject) {
            if (!device_mqtt_parse_subject(cursor, fields)) {
                return false;
            }
        } else if (is_payload) {
            if (!device_mqtt_parse_payload(cursor, fields)) {
                return false;
            }
        } else if (!device_mqtt_skip_json_value(cursor)) {
            return false;
        }
        device_mqtt_skip_whitespace(cursor);
        if (**cursor == ',') {
            *cursor += 1;
            device_mqtt_skip_whitespace(cursor);
            continue;
        }
        if (**cursor == '}') {
            *cursor += 1;
            return true;
        }
        return false;
    }
    return false;
}

static bool device_mqtt_identifier_is_valid(
    const char *value,
    size_t length,
    size_t max_length
) {
    if (value == NULL || length == 0 || length > max_length) {
        return false;
    }
    if (value[0] < 'a' || value[0] > 'z') {
        return false;
    }
    size_t segment_count = 0;
    bool previous_was_separator = false;
    for (size_t index = 1; index < length; ++index) {
        const unsigned char current = (unsigned char)value[index];
        if (current == '_') {
            if (previous_was_separator) {
                return false;
            }
            previous_was_separator = true;
            segment_count += 1;
            if (segment_count > 7) {
                return false;
            }
            continue;
        }
        const bool is_lower = current >= 'a' && current <= 'z';
        const bool is_digit = current >= '0' && current <= '9';
        if (!is_lower && !is_digit) {
            return false;
        }
        previous_was_separator = false;
    }
    return segment_count > 0 && !previous_was_separator;
}

static bool device_mqtt_command_type_is_valid(
    const char *value,
    size_t length
) {
    return device_mqtt_identifier_is_valid(
        value,
        length,
        DEVICE_MQTT_COMMAND_TYPE_SIZE - 1
    );
}

static bool device_mqtt_uuid_is_valid(const char *value, size_t length) {
    if (value == NULL || length != 36) {
        return false;
    }
    for (size_t index = 0; index < 36; ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') {
                return false;
            }
            continue;
        }
        const unsigned char current = (unsigned char)value[index];
        const bool is_digit = current >= '0' && current <= '9';
        const bool is_lower_hex = current >= 'a' && current <= 'f';
        if (!is_digit && !is_lower_hex) {
            return false;
        }
    }
    return true;
}

static bool device_mqtt_command_id_is_valid(
    const char *value,
    size_t length
) {
    return device_mqtt_uuid_is_valid(value, length) ||
        device_mqtt_identifier_is_valid(
            value,
            length,
            DEVICE_MQTT_COMMAND_ID_SIZE - 1
        );
}

static bool device_mqtt_string_equals(
    const char *value,
    size_t length,
    const char *expected
) {
    const size_t expected_length = strlen(expected);
    return value != NULL && length == expected_length &&
        memcmp(value, expected, expected_length) == 0;
}

static device_mqtt_error_t device_mqtt_copy_checked(
    const char *source,
    size_t source_length,
    char *destination,
    size_t destination_size
) {
    if (source == NULL || destination == NULL || destination_size == 0) {
        return DEVICE_MQTT_ERR_INVALID_SUBJECT;
    }
    if (source_length >= destination_size) {
        return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
    }
    memcpy(destination, source, source_length);
    destination[source_length] = '\0';
    return DEVICE_MQTT_OK;
}

static bool device_mqtt_json_token_is_safe(
    const char *value,
    size_t max_length
) {
    if (value == NULL) {
        return false;
    }
    const size_t length = strlen(value);
    if (length == 0 || length > max_length) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const unsigned char current = (unsigned char)value[index];
        if (current < 0x20 || current == '"' || current == '\\') {
            return false;
        }
    }
    return true;
}

device_mqtt_error_t device_mqtt_device_id_from_topic(
    const char *topic,
    char *device_id_out,
    size_t device_id_out_size
) {
    static const char prefix[] = "sprout/v1/devices/";
    static const char suffix[] = "/commands";
    if (topic == NULL || device_id_out == NULL || device_id_out_size == 0) {
        return DEVICE_MQTT_ERR_NULL_ARGUMENT;
    }
    const size_t topic_length = strlen(topic);
    const size_t prefix_length = sizeof(prefix) - 1;
    const size_t suffix_length = sizeof(suffix) - 1;
    if (topic_length <= prefix_length + suffix_length ||
        strncmp(topic, prefix, prefix_length) != 0 ||
        strcmp(topic + topic_length - suffix_length, suffix) != 0) {
        return DEVICE_MQTT_ERR_INVALID_TOPIC;
    }
    const size_t device_length =
        topic_length - prefix_length - suffix_length;
    const char *device_id = topic + prefix_length;
    if (device_length == 0 ||
        device_length >= DEVICE_MQTT_DEVICE_ID_SIZE ||
        memchr(device_id, '/', device_length) != NULL ||
        !device_mqtt_identifier_is_valid(
            device_id,
            device_length,
            DEVICE_MQTT_DEVICE_ID_SIZE - 1
        )) {
        return DEVICE_MQTT_ERR_INVALID_TOPIC;
    }
    memcpy(device_id_out, device_id, device_length);
    device_id_out[device_length] = '\0';
    return DEVICE_MQTT_OK;
}

device_mqtt_error_t device_mqtt_parse_command(
    const char *device_id,
    const char *payload,
    size_t payload_length,
    device_mqtt_command_t *command_out
) {
    if (device_id == NULL || payload == NULL || command_out == NULL) {
        return DEVICE_MQTT_ERR_NULL_ARGUMENT;
    }
    if (!device_mqtt_identifier_is_valid(
            device_id,
            strlen(device_id),
            DEVICE_MQTT_DEVICE_ID_SIZE - 1
        )) {
        return DEVICE_MQTT_ERR_INVALID_SUBJECT;
    }
    if (payload_length == 0) {
        return DEVICE_MQTT_ERR_INVALID_JSON;
    }
    char bounded_payload[2048] = {0};
    if (payload_length >= sizeof(bounded_payload)) {
        return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
    }
    memcpy(bounded_payload, payload, payload_length);

    const char *cursor = bounded_payload;
    device_mqtt_skip_whitespace(&cursor);
    device_mqtt_envelope_fields_t fields;
    memset(&fields, 0, sizeof(fields));
    if (!device_mqtt_parse_envelope(&cursor, &fields)) {
        return DEVICE_MQTT_ERR_INVALID_JSON;
    }
    device_mqtt_skip_whitespace(&cursor);
    if (*cursor != '\0') {
        return DEVICE_MQTT_ERR_INVALID_JSON;
    }
    if (!device_mqtt_string_equals(
            fields.schema_version,
            fields.schema_version_length,
            DEVICE_MQTT_SCHEMA_VERSION
        )) {
        return DEVICE_MQTT_ERR_UNSUPPORTED_SCHEMA;
    }
    if (!device_mqtt_string_equals(
            fields.event_type,
            fields.event_type_length,
            DEVICE_MQTT_COMMAND_EVENT_TYPE
        )) {
        return DEVICE_MQTT_ERR_UNSUPPORTED_EVENT;
    }
    if (!device_mqtt_string_equals(
            fields.subject_type,
            fields.subject_type_length,
            "device"
        ) ||
        fields.subject_id == NULL ||
        fields.subject_id_length != strlen(device_id) ||
        memcmp(fields.subject_id, device_id, fields.subject_id_length) != 0) {
        return DEVICE_MQTT_ERR_DEVICE_MISMATCH;
    }
    if (fields.request_id == NULL || fields.request_id_length == 0 ||
        fields.command_id == NULL || fields.command_id_length == 0 ||
        fields.command_type == NULL || fields.command_type_length == 0 ||
        fields.payload_request_id == NULL ||
        fields.payload_request_id_length == 0) {
        return DEVICE_MQTT_ERR_MISSING_FIELD;
    }
    const bool command_id_valid = device_mqtt_command_id_is_valid(
        fields.command_id,
        fields.command_id_length
    );
    if (!command_id_valid) {
        return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
    }
    if (!device_mqtt_command_type_is_valid(
            fields.command_type,
            fields.command_type_length
        )) {
        return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
    }
    if (fields.request_id_length > DEVICE_MQTT_REQUEST_ID_SIZE - 1 ||
        fields.payload_request_id_length > DEVICE_MQTT_REQUEST_ID_SIZE - 1 ||
        fields.payload_request_id_length != fields.request_id_length ||
        memcmp(
            fields.request_id,
            fields.payload_request_id,
            fields.request_id_length
        ) != 0) {
        return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
    }
    if (fields.data_length >= DEVICE_MQTT_COMMAND_DATA_SIZE) {
        return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
    }
    memset(command_out, 0, sizeof(*command_out));
    snprintf(command_out->device_id, sizeof(command_out->device_id), "%s",
             device_id);
    if (device_mqtt_copy_checked(
            fields.command_id,
            fields.command_id_length,
            command_out->command_id,
            sizeof(command_out->command_id)
        ) != DEVICE_MQTT_OK ||
        device_mqtt_copy_checked(
            fields.command_type,
            fields.command_type_length,
            command_out->command_type,
            sizeof(command_out->command_type)
        ) != DEVICE_MQTT_OK ||
        device_mqtt_copy_checked(
            fields.request_id,
            fields.request_id_length,
            command_out->request_id,
            sizeof(command_out->request_id)
        ) != DEVICE_MQTT_OK) {
        return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
    }
    if (fields.data != NULL) {
        if (device_mqtt_copy_checked(
                fields.data,
                fields.data_length,
                command_out->data,
                sizeof(command_out->data)
            ) != DEVICE_MQTT_OK) {
            return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
        }
        command_out->data_length = fields.data_length;
    }
    return DEVICE_MQTT_OK;
}

void device_mqtt_reset_state(void) {
    memset(&device_mqtt_ledger, 0, sizeof(device_mqtt_ledger));
}

static bool device_mqtt_data_object(
    const device_mqtt_command_t *command,
    const char **cursor_out
) {
    if (command == NULL || command->data_length == 0) {
        return false;
    }
    if (command->data_length >= sizeof(command->data) ||
        command->data[command->data_length] != '\0') {
        return false;
    }
    const char *cursor = command->data;
    device_mqtt_skip_whitespace(&cursor);
    if (*cursor != '{') {
        return false;
    }
    *cursor_out = cursor;
    return true;
}

static bool device_mqtt_next_data_key(
    const char **cursor,
    const char **key_out,
    size_t *key_length_out
) {
    device_mqtt_skip_whitespace(cursor);
    if (**cursor != '"') {
        return false;
    }
    const char *key = NULL;
    size_t key_length = 0;
    if (!device_mqtt_read_json_string(cursor, &key, &key_length)) {
        return false;
    }
    device_mqtt_skip_whitespace(cursor);
    if (**cursor != ':') {
        return false;
    }
    *cursor += 1;
    device_mqtt_skip_whitespace(cursor);
    *key_out = key;
    *key_length_out = key_length;
    return true;
}

static bool device_mqtt_advance_data_object(
    const char **cursor,
    bool *done_out
) {
    device_mqtt_skip_whitespace(cursor);
    if (**cursor == ',') {
        *cursor += 1;
        *done_out = false;
        return true;
    }
    if (**cursor != '}') {
        return false;
    }
    *cursor += 1;
    device_mqtt_skip_whitespace(cursor);
    if (**cursor != '\0') {
        return false;
    }
    *done_out = true;
    return true;
}

device_mqtt_error_t device_mqtt_parse_display_message(
    const device_mqtt_command_t *command,
    device_mqtt_display_message_t *message_out
) {
    if (command == NULL || message_out == NULL) {
        return DEVICE_MQTT_ERR_NULL_ARGUMENT;
    }
    if (strcmp(command->command_type, "display_message") != 0) {
        return DEVICE_MQTT_ERR_UNSUPPORTED_COMMAND;
    }
    memset(message_out, 0, sizeof(*message_out));

    const char *cursor = NULL;
    if (!device_mqtt_data_object(command, &cursor)) {
        return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
    }
    cursor += 1;
    device_mqtt_skip_whitespace(&cursor);
    if (*cursor == '}') {
        return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
    }
    bool has_notification_id = false;
    bool has_title = false;
    bool has_body = false;
    bool has_severity = false;
    bool has_category = false;
    bool has_duration = false;
    bool done = false;
    while (*cursor != '\0') {
        const char *key = NULL;
        size_t key_length = 0;
        if (!device_mqtt_next_data_key(&cursor, &key, &key_length)) {
            return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
        }
        if (key_length == 15 &&
            memcmp(key, "notification_id", 15) == 0) {
            if (!device_mqtt_read_decoded_json_string(
                    &cursor,
                    message_out->notification_id,
                    sizeof(message_out->notification_id)
                )) {
                return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
            }
            has_notification_id = true;
        } else if (key_length == 5 && memcmp(key, "title", 5) == 0) {
            if (!device_mqtt_read_decoded_json_string(
                    &cursor,
                    message_out->title,
                    sizeof(message_out->title)
                )) {
                return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
            }
            has_title = true;
        } else if (key_length == 4 && memcmp(key, "body", 4) == 0) {
            if (!device_mqtt_read_decoded_json_string(
                    &cursor,
                    message_out->body,
                    sizeof(message_out->body)
                )) {
                return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
            }
            has_body = true;
        } else if (key_length == 8 && memcmp(key, "severity", 8) == 0) {
            if (!device_mqtt_read_decoded_json_string(
                    &cursor,
                    message_out->severity,
                    sizeof(message_out->severity)
                )) {
                return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
            }
            has_severity = true;
        } else if (key_length == 8 && memcmp(key, "category", 8) == 0) {
            if (!device_mqtt_read_decoded_json_string(
                    &cursor,
                    message_out->category,
                    sizeof(message_out->category)
                )) {
                return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
            }
            has_category = true;
        } else if (key_length == 16 &&
                   memcmp(key, "duration_seconds", 16) == 0) {
            if (!device_mqtt_read_u32_value(
                    &cursor,
                    &message_out->duration_seconds
                )) {
                return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
            }
            has_duration = true;
        } else if (!device_mqtt_skip_json_value(&cursor)) {
            return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
        }
        if (!device_mqtt_advance_data_object(&cursor, &done)) {
            return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
        }
        if (done) {
            break;
        }
    }
    if (!has_notification_id || !has_title ||
        message_out->notification_id[0] == '\0' ||
        message_out->title[0] == '\0') {
        return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
    }
    (void)has_body;
    (void)has_severity;
    (void)has_category;
    (void)has_duration;
    return DEVICE_MQTT_OK;
}

device_mqtt_error_t device_mqtt_parse_privacy_consent(
    const device_mqtt_command_t *command,
    device_mqtt_privacy_consent_t *consent_out
) {
    if (command == NULL || consent_out == NULL) {
        return DEVICE_MQTT_ERR_NULL_ARGUMENT;
    }
    if (strcmp(command->command_type, "sync_privacy_consent") != 0) {
        return DEVICE_MQTT_ERR_UNSUPPORTED_COMMAND;
    }
    memset(consent_out, 0, sizeof(*consent_out));

    const char *cursor = NULL;
    if (!device_mqtt_data_object(command, &cursor)) {
        return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
    }
    cursor += 1;
    device_mqtt_skip_whitespace(&cursor);
    if (*cursor == '}') {
        return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
    }
    bool has_audio_upload = false;
    bool has_image_upload = false;
    bool has_conversation_history = false;
    bool has_usage_analytics = false;
    bool done = false;
    while (*cursor != '\0') {
        const char *key = NULL;
        size_t key_length = 0;
        if (!device_mqtt_next_data_key(&cursor, &key, &key_length)) {
            return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
        }
        if (key_length == 12 && memcmp(key, "audio_upload", 12) == 0) {
            if (!device_mqtt_read_bool_value(
                    &cursor,
                    &consent_out->audio_upload_consent
                )) {
                return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
            }
            has_audio_upload = true;
        } else if (key_length == 12 &&
                   memcmp(key, "image_upload", 12) == 0) {
            if (!device_mqtt_read_bool_value(
                    &cursor,
                    &consent_out->image_upload_consent
                )) {
                return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
            }
            has_image_upload = true;
        } else if (key_length == 20 &&
                   memcmp(key, "conversation_history", 20) == 0) {
            if (!device_mqtt_read_bool_value(
                    &cursor,
                    &consent_out->conversation_history_consent
                )) {
                return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
            }
            has_conversation_history = true;
        } else if (key_length == 15 &&
                   memcmp(key, "usage_analytics", 15) == 0) {
            if (!device_mqtt_read_bool_value(
                    &cursor,
                    &consent_out->usage_analytics_consent
                )) {
                return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
            }
            has_usage_analytics = true;
        } else if (!device_mqtt_skip_json_value(&cursor)) {
            return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
        }
        if (!device_mqtt_advance_data_object(&cursor, &done)) {
            return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
        }
        if (done) {
            break;
        }
    }
    if (!has_audio_upload || !has_image_upload ||
        !has_conversation_history || !has_usage_analytics) {
        return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
    }
    return DEVICE_MQTT_OK;
}

device_mqtt_error_t device_mqtt_lookup_command(
    const char *command_id,
    device_mqtt_command_record_t *command_out
) {
    if (command_id == NULL || command_out == NULL) {
        return DEVICE_MQTT_ERR_NULL_ARGUMENT;
    }
    if (command_id[0] == '\0' ||
        !device_mqtt_command_id_is_valid(
            command_id,
            strlen(command_id)
        )) {
        return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
    }
    const size_t length = strlen(command_id);
    for (size_t index = 0; index < device_mqtt_ledger.count; ++index) {
        const device_mqtt_command_record_t *record =
            &device_mqtt_ledger.records[index];
        if (strlen(record->command_id) == length &&
            memcmp(record->command_id, command_id, length) == 0) {
            *command_out = *record;
            return DEVICE_MQTT_OK;
        }
    }
    return DEVICE_MQTT_ERR_NOT_FOUND;
}

bool device_mqtt_is_duplicate(const char *command_id) {
    device_mqtt_command_record_t record = {};
    return device_mqtt_lookup_command(command_id, &record) == DEVICE_MQTT_OK;
}

device_mqtt_error_t device_mqtt_record_command(
    const device_mqtt_command_t *command,
    device_mqtt_command_status_t *status_out
) {
    if (command == NULL || status_out == NULL) {
        return DEVICE_MQTT_ERR_NULL_ARGUMENT;
    }
    if (!device_mqtt_command_id_is_valid(
            command->command_id,
            strlen(command->command_id)
        )) {
        return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
    }
    device_mqtt_command_record_t existing = {};
    if (device_mqtt_lookup_command(command->command_id, &existing) ==
        DEVICE_MQTT_OK) {
        *status_out = DEVICE_MQTT_COMMAND_STATUS_DUPLICATE;
        return DEVICE_MQTT_OK;
    }
    const size_t slot = device_mqtt_ledger.count <
            DEVICE_MQTT_DEDUPE_CAPACITY
        ? device_mqtt_ledger.count
        : device_mqtt_ledger.next;
    memset(
        &device_mqtt_ledger.records[slot],
        0,
        sizeof(device_mqtt_ledger.records[slot])
    );
    snprintf(
        device_mqtt_ledger.records[slot].command_id,
        sizeof(device_mqtt_ledger.records[slot].command_id),
        "%s",
        command->command_id
    );
    snprintf(
        device_mqtt_ledger.records[slot].request_id,
        sizeof(device_mqtt_ledger.records[slot].request_id),
        "%s",
        command->request_id
    );
    if (device_mqtt_ledger.count < DEVICE_MQTT_DEDUPE_CAPACITY) {
        device_mqtt_ledger.count += 1;
    } else {
        device_mqtt_ledger.next =
            (device_mqtt_ledger.next + 1) % DEVICE_MQTT_DEDUPE_CAPACITY;
    }
    *status_out = DEVICE_MQTT_COMMAND_STATUS_NEW;
    return DEVICE_MQTT_OK;
}

device_mqtt_error_t device_mqtt_complete_command(
    const char *command_id,
    const char *request_id,
    const char *status,
    const char *result_code
) {
    if (command_id == NULL || request_id == NULL || status == NULL ||
        result_code == NULL) {
        return DEVICE_MQTT_ERR_NULL_ARGUMENT;
    }
    if (strcmp(status, "acknowledged") != 0 &&
        strcmp(status, "failed") != 0) {
        return DEVICE_MQTT_ERR_INVALID_PAYLOAD;
    }
    if (!device_mqtt_json_token_is_safe(
            request_id,
            DEVICE_MQTT_REQUEST_ID_SIZE - 1
        ) ||
        !device_mqtt_json_token_is_safe(
            result_code,
            DEVICE_MQTT_ACK_RESULT_CODE_SIZE - 1
        )) {
        return DEVICE_MQTT_ERR_FIELD_TOO_LONG;
    }
    const size_t length = strlen(command_id);
    for (size_t index = 0; index < device_mqtt_ledger.count; ++index) {
        device_mqtt_command_record_t *record =
            &device_mqtt_ledger.records[index];
        if (strlen(record->command_id) == length &&
            memcmp(record->command_id, command_id, length) == 0) {
            snprintf(
                record->request_id,
                sizeof(record->request_id),
                "%s",
                request_id
            );
            snprintf(
                record->status,
                sizeof(record->status),
                "%s",
                status
            );
            snprintf(
                record->result_code,
                sizeof(record->result_code),
                "%s",
                result_code
            );
            record->completed = true;
            return DEVICE_MQTT_OK;
        }
    }
    return DEVICE_MQTT_ERR_NOT_FOUND;
}

static esp_err_t device_mqtt_append(
    char *json_out,
    size_t json_out_size,
    size_t *offset,
    const char *value
) {
    const size_t length = strlen(value);
    if (*offset >= json_out_size ||
        length > json_out_size - *offset - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(json_out + *offset, value, length);
    *offset += length;
    json_out[*offset] = '\0';
    return ESP_OK;
}

static uint64_t device_mqtt_fnv1a64(const char *value) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != '\0';
         ++cursor) {
        hash ^= (uint64_t)*cursor;
        hash *= 1099511628211ULL;
    }
    return hash;
}

esp_err_t device_mqtt_build_command_ack(
    const char *device_id,
    const char *command_id,
    const char *request_id,
    const char *occurred_at,
    const char *status,
    const char *result_code,
    char *json_out,
    size_t json_out_size
) {
    if (device_id == NULL || command_id == NULL || request_id == NULL ||
        occurred_at == NULL || status == NULL || result_code == NULL ||
        json_out == NULL || json_out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!device_mqtt_identifier_is_valid(
            device_id,
            strlen(device_id),
            DEVICE_MQTT_DEVICE_ID_SIZE - 1
        ) ||
        !device_mqtt_command_id_is_valid(
            command_id,
            strlen(command_id)
        ) ||
        !device_mqtt_json_token_is_safe(
            request_id,
            DEVICE_MQTT_REQUEST_ID_SIZE - 1
        ) ||
        !device_mqtt_json_token_is_safe(occurred_at, 40) ||
        !device_mqtt_json_token_is_safe(result_code, 64) ||
        (strcmp(status, "acknowledged") != 0 &&
         strcmp(status, "failed") != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    json_out[0] = '\0';
    char event_id[32] = {0};
    const unsigned long long command_hash =
        (unsigned long long)device_mqtt_fnv1a64(command_id);
    const int event_id_length = snprintf(
        event_id,
        sizeof(event_id),
        "command_ack_%016llx",
        command_hash
    );
    if (event_id_length < 0 ||
        (size_t)event_id_length >= sizeof(event_id)) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t offset = 0;
    if (device_mqtt_append(
            json_out,
            json_out_size,
            &offset,
            "{\"schema_version\":\"1.0.0\",\"event_id\":\""
        ) != ESP_OK ||
        device_mqtt_append(
            json_out, json_out_size, &offset, event_id
        ) != ESP_OK ||
        device_mqtt_append(
            json_out,
            json_out_size,
            &offset,
            "\",\"event_type\":\"device.command_ack\",\"occurred_at\":\""
        ) != ESP_OK ||
        device_mqtt_append(
            json_out, json_out_size, &offset, occurred_at
        ) != ESP_OK ||
        device_mqtt_append(
            json_out,
            json_out_size,
            &offset,
            "\",\"producer\":\"firmware\",\"subject\":{\"type\":\"device\","
            "\"id\":\""
        ) != ESP_OK ||
        device_mqtt_append(
            json_out, json_out_size, &offset, device_id
        ) != ESP_OK ||
        device_mqtt_append(
            json_out,
            json_out_size,
            &offset,
            "\"},\"request_id\":\""
        ) != ESP_OK ||
        device_mqtt_append(
            json_out, json_out_size, &offset, request_id
        ) != ESP_OK ||
        device_mqtt_append(
            json_out,
            json_out_size,
            &offset,
            "\",\"payload\":{\"command_id\":\""
        ) != ESP_OK ||
        device_mqtt_append(
            json_out, json_out_size, &offset, command_id
        ) != ESP_OK ||
        device_mqtt_append(
            json_out, json_out_size, &offset, "\",\"status\":\""
        ) != ESP_OK ||
        device_mqtt_append(
            json_out, json_out_size, &offset, status
        ) != ESP_OK ||
        device_mqtt_append(
            json_out,
            json_out_size,
            &offset,
            "\",\"result_code\":\""
        ) != ESP_OK ||
        device_mqtt_append(
            json_out, json_out_size, &offset, result_code
        ) != ESP_OK ||
        device_mqtt_append(
            json_out,
            json_out_size,
            &offset,
            "\",\"request_id\":\""
        ) != ESP_OK ||
        device_mqtt_append(
            json_out, json_out_size, &offset, request_id
        ) != ESP_OK ||
        device_mqtt_append(
            json_out, json_out_size, &offset, "\"}}"
        ) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

const char *device_mqtt_error_name(device_mqtt_error_t error) {
    switch (error) {
        case DEVICE_MQTT_OK:
            return "ok";
        case DEVICE_MQTT_ERR_NULL_ARGUMENT:
            return "null_argument";
        case DEVICE_MQTT_ERR_INVALID_TOPIC:
            return "invalid_topic";
        case DEVICE_MQTT_ERR_DEVICE_MISMATCH:
            return "device_mismatch";
        case DEVICE_MQTT_ERR_NOT_FOUND:
            return "not_found";
        case DEVICE_MQTT_ERR_INVALID_JSON:
            return "invalid_json";
        case DEVICE_MQTT_ERR_UNSUPPORTED_SCHEMA:
            return "unsupported_schema";
        case DEVICE_MQTT_ERR_UNSUPPORTED_EVENT:
            return "unsupported_event";
        case DEVICE_MQTT_ERR_INVALID_SUBJECT:
            return "invalid_subject";
        case DEVICE_MQTT_ERR_MISSING_FIELD:
            return "missing_field";
        case DEVICE_MQTT_ERR_FIELD_TOO_LONG:
            return "field_too_long";
        case DEVICE_MQTT_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case DEVICE_MQTT_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}
