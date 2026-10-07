#include "voice_session_protocol.h"

#include <stdio.h>
#include <string.h>

static uint16_t read_u16_be(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] << 8 | (uint16_t)data[1]);
}

static uint32_t read_u32_be(const uint8_t *data) {
    return (uint32_t)data[0] << 24 |
           (uint32_t)data[1] << 16 |
           (uint32_t)data[2] << 8 |
           (uint32_t)data[3];
}

static uint64_t read_u64_be(const uint8_t *data) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value = (value << 8) | (uint64_t)data[index];
    }
    return value;
}

static void write_u16_be(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)(value & 0xFF);
}

static void write_u32_be(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)(value & 0xFF);
}

static void write_u64_be(uint8_t *data, uint64_t value) {
    for (size_t index = 0; index < 8; ++index) {
        data[index] = (uint8_t)(value >> (56 - index * 8));
    }
}

bool voice_session_identifier_is_valid(const char *identifier) {
    if (identifier == NULL) {
        return false;
    }
    const size_t length = strlen(identifier);
    if (length == 0 || length >= VOICE_SESSION_IDENTIFIER_SIZE) {
        return false;
    }
    if (identifier[0] < 'a' || identifier[0] > 'z') {
        return false;
    }

    // The contract requires at least one underscore group, so count the
    // separators and reject empty groups the gateway would refuse.
    size_t underscore_count = 0;
    bool previous_underscore = false;
    for (size_t index = 1; index < length; ++index) {
        const char character = identifier[index];
        if (character == '_') {
            if (previous_underscore) {
                return false;
            }
            previous_underscore = true;
            underscore_count++;
            continue;
        }
        if ((character < 'a' || character > 'z') &&
            (character < '0' || character > '9')) {
            return false;
        }
        previous_underscore = false;
    }
    if (previous_underscore || underscore_count < 1 || underscore_count > 7) {
        return false;
    }
    return true;
}

bool voice_session_identifier_generate(
    const char *prefix,
    uint32_t random_value,
    char *output,
    size_t output_size
) {
    if (prefix == NULL || output == NULL || output_size == 0) {
        return false;
    }
    const size_t prefix_length = strlen(prefix);
    if (prefix_length == 0 || prefix_length > 100) {
        return false;
    }
    for (size_t index = 0; index < prefix_length; ++index) {
        const char character = prefix[index];
        if ((character < 'a' || character > 'z') &&
            (character < '0' || character > '9') &&
            character != '_') {
            return false;
        }
    }
    if (prefix[0] < 'a' || prefix[0] > 'z') {
        return false;
    }

    const int written = snprintf(
        output,
        output_size,
        "%s_%08lx",
        prefix,
        (unsigned long)random_value
    );
    if (written <= 0 || (size_t)written >= output_size) {
        return false;
    }
    return voice_session_identifier_is_valid(output);
}

bool voice_session_state_from_name(
    const char *state_name,
    voice_session_protocol_state_t *state_out
) {
    if (state_name == NULL || state_out == NULL) {
        return false;
    }
    if (strcmp(state_name, "idle") == 0) {
        *state_out = VOICE_SESSION_PROTOCOL_STATE_IDLE;
        return true;
    }
    if (strcmp(state_name, "listening") == 0) {
        *state_out = VOICE_SESSION_PROTOCOL_STATE_LISTENING;
        return true;
    }
    if (strcmp(state_name, "thinking") == 0) {
        *state_out = VOICE_SESSION_PROTOCOL_STATE_THINKING;
        return true;
    }
    if (strcmp(state_name, "speaking") == 0) {
        *state_out = VOICE_SESSION_PROTOCOL_STATE_SPEAKING;
        return true;
    }
    return false;
}

const char *voice_session_protocol_state_name(
    voice_session_protocol_state_t state
) {
    switch (state) {
        case VOICE_SESSION_PROTOCOL_STATE_IDLE:
            return "idle";
        case VOICE_SESSION_PROTOCOL_STATE_LISTENING:
            return "listening";
        case VOICE_SESSION_PROTOCOL_STATE_THINKING:
            return "thinking";
        case VOICE_SESSION_PROTOCOL_STATE_SPEAKING:
            return "speaking";
        default:
            return NULL;
    }
}

static bool magic_is_valid(voice_session_envelope_magic_t magic) {
    return magic == VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO ||
           magic == VOICE_SESSION_ENVELOPE_MAGIC_SERVER_AUDIO;
}

voice_session_protocol_error_t voice_session_envelope_encode(
    voice_session_envelope_magic_t magic,
    const char *session_id,
    uint32_t sequence,
    uint64_t timestamp_millis,
    const uint8_t *payload,
    size_t payload_length,
    uint8_t *output,
    size_t output_size,
    size_t *output_length
) {
    if (!magic_is_valid(magic) || session_id == NULL || payload == NULL ||
        output == NULL || output_length == NULL) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_ARGUMENT;
    }
    if (!voice_session_identifier_is_valid(session_id)) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_IDENTIFIER;
    }
    if (payload_length == 0 ||
        payload_length > VOICE_SESSION_MAX_PAYLOAD_BYTES) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH;
    }

    const size_t session_id_length = strlen(session_id);
    const size_t header_length =
        VOICE_SESSION_ENVELOPE_HEADER_SIZE + session_id_length;
    const size_t total_length = header_length + payload_length;
    if (output_size < total_length) {
        return VOICE_SESSION_PROTOCOL_ERR_OUTPUT_TOO_SMALL;
    }

    static const uint8_t device_magic[4] = {'S', 'R', 'A', 'W'};
    static const uint8_t server_magic[4] = {'S', 'R', 'S', 'V'};
    const uint8_t *magic_bytes =
        magic == VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO
            ? device_magic
            : server_magic;
    memcpy(output, magic_bytes, 4);
    output[4] = (uint8_t)VOICE_SESSION_ENVELOPE_VERSION;
    output[5] = 0;
    write_u16_be(&output[6], (uint16_t)header_length);
    write_u32_be(&output[8], sequence);
    write_u64_be(&output[12], timestamp_millis);
    write_u16_be(&output[20], (uint16_t)session_id_length);
    write_u16_be(&output[22], (uint16_t)payload_length);
    memcpy(&output[24], session_id, session_id_length);
    memcpy(&output[header_length], payload, payload_length);
    *output_length = total_length;
    return VOICE_SESSION_PROTOCOL_OK;
}

voice_session_protocol_error_t voice_session_envelope_decode(
    const uint8_t *input,
    size_t input_length,
    voice_session_envelope_magic_t expected_magic,
    voice_session_envelope_t *envelope_out
) {
    if (input == NULL || envelope_out == NULL) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_ARGUMENT;
    }
    if (input_length < VOICE_SESSION_ENVELOPE_HEADER_SIZE) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH;
    }

    voice_session_envelope_magic_t magic = VOICE_SESSION_ENVELOPE_MAGIC_NONE;
    if (memcmp(input, "SRAW", 4) == 0) {
        magic = VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO;
    } else if (memcmp(input, "SRSV", 4) == 0) {
        magic = VOICE_SESSION_ENVELOPE_MAGIC_SERVER_AUDIO;
    } else {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_MAGIC;
    }
    if (expected_magic != VOICE_SESSION_ENVELOPE_MAGIC_NONE &&
        expected_magic != magic) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_MAGIC;
    }
    if (input[4] != (uint8_t)VOICE_SESSION_ENVELOPE_VERSION) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_VERSION;
    }
    if (input[5] != 0) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_VERSION;
    }

    const size_t header_length = read_u16_be(&input[6]);
    const uint32_t sequence = read_u32_be(&input[8]);
    const uint64_t timestamp_millis = read_u64_be(&input[12]);
    const size_t session_id_length = read_u16_be(&input[20]);
    const size_t payload_length = read_u16_be(&input[22]);

    if (session_id_length == 0 ||
        session_id_length >= VOICE_SESSION_IDENTIFIER_SIZE) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH;
    }
    if (payload_length == 0 ||
        payload_length > VOICE_SESSION_MAX_PAYLOAD_BYTES) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH;
    }
    if (header_length !=
        VOICE_SESSION_ENVELOPE_HEADER_SIZE + session_id_length) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH;
    }
    if (input_length != header_length + payload_length) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH;
    }

    memset(envelope_out, 0, sizeof(*envelope_out));
    memcpy(envelope_out->session_id, &input[24], session_id_length);
    envelope_out->session_id[session_id_length] = '\0';
    if (!voice_session_identifier_is_valid(envelope_out->session_id)) {
        return VOICE_SESSION_PROTOCOL_ERR_INVALID_IDENTIFIER;
    }
    envelope_out->magic = magic;
    envelope_out->sequence = sequence;
    envelope_out->timestamp_millis = timestamp_millis;
    envelope_out->payload = &input[header_length];
    envelope_out->payload_length = payload_length;
    return VOICE_SESSION_PROTOCOL_OK;
}
