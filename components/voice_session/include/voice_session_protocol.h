#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Voice control schema version shared with the gateway contract. */
#define VOICE_SESSION_PROTOCOL_SCHEMA_VERSION "1.0.0"

/** @brief Fixed binary envelope header size before the session identifier. */
#define VOICE_SESSION_ENVELOPE_HEADER_SIZE 24

/** @brief Envelope format version carried in byte 4 for audio envelopes. */
#define VOICE_SESSION_ENVELOPE_VERSION 1u

/**
 * @brief Device-video envelope header size before the session identifier.
 *
 * A still is far larger than one Opus frame, so the video envelope widens the
 * payload length from 16 to 32 bits. That growth is why it carries its own
 * header size and version instead of reusing the audio layout.
 */
#define VOICE_SESSION_VIDEO_ENVELOPE_HEADER_SIZE 26

/** @brief Envelope format version carried in byte 4 for device-video envelopes. */
#define VOICE_SESSION_VIDEO_ENVELOPE_VERSION 2u

/** @brief Maximum accepted Opus payload, matching the gateway read window. */
#define VOICE_SESSION_MAX_PAYLOAD_BYTES 1024

/**
 * @brief Maximum accepted encoded video still payload.
 *
 * A single JPEG still is far larger than one Opus frame, so the video magic
 * carries its own cap instead of widening the audio window and draws its
 * payload length from the 32-bit field of the video envelope.
 */
#define VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES 262144

/** @brief Maximum session or stream identifier length including terminator. */
#define VOICE_SESSION_IDENTIFIER_SIZE 128

/**
 * @brief Envelope magic values.
 *
 * Device audio is SRAW, gateway audio is SRSV, device video is SRVD.
 */
typedef enum {
    VOICE_SESSION_ENVELOPE_MAGIC_NONE = 0,
    VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO,
    VOICE_SESSION_ENVELOPE_MAGIC_SERVER_AUDIO,
    VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
} voice_session_envelope_magic_t;

/** @brief Errors returned by the pure protocol helpers. */
typedef enum {
    VOICE_SESSION_PROTOCOL_OK = 0,
    VOICE_SESSION_PROTOCOL_ERR_INVALID_ARGUMENT,
    VOICE_SESSION_PROTOCOL_ERR_INVALID_IDENTIFIER,
    VOICE_SESSION_PROTOCOL_ERR_INVALID_MAGIC,
    VOICE_SESSION_PROTOCOL_ERR_INVALID_VERSION,
    VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH,
    VOICE_SESSION_PROTOCOL_ERR_OUTPUT_TOO_SMALL,
} voice_session_protocol_error_t;

/** @brief Parsed view over one binary audio envelope. */
typedef struct {
    voice_session_envelope_magic_t magic;
    uint32_t sequence;
    uint64_t timestamp_millis;
    char session_id[VOICE_SESSION_IDENTIFIER_SIZE];
    const uint8_t *payload;
    size_t payload_length;
} voice_session_envelope_t;

/** @brief Conversation state mirror from the gateway session_state frame. */
typedef enum {
    VOICE_SESSION_PROTOCOL_STATE_IDLE = 0,
    VOICE_SESSION_PROTOCOL_STATE_LISTENING,
    VOICE_SESSION_PROTOCOL_STATE_THINKING,
    VOICE_SESSION_PROTOCOL_STATE_SPEAKING,
} voice_session_protocol_state_t;

/**
 * @brief Validate an identifier against the shared lowercase snake_case rule.
 *
 * Mirrors the gateway pattern ^[a-z][a-z0-9]*(?:_[a-z0-9]+){1,7}$ so a device
 * never sends a value the gateway will reject.
 */
bool voice_session_identifier_is_valid(const char *identifier);

/**
 * @brief Generate a contract-valid identifier from a lowercase prefix.
 *
 * The result is "<prefix>_<8 hex digits>", which satisfies the identifier
 * pattern for any valid lowercase prefix. random_value lets a host test pin
 * the output. Returns false when the prefix or output buffer is unusable.
 */
bool voice_session_identifier_generate(
    const char *prefix,
    uint32_t random_value,
    char *output,
    size_t output_size
);

/** @brief Map a session state name to its enum value. */
bool voice_session_state_from_name(
    const char *state_name,
    voice_session_protocol_state_t *state_out
);

/** @brief Return the canonical session state name, or NULL when invalid. */
const char *voice_session_protocol_state_name(
    voice_session_protocol_state_t state
);

/**
 * @brief Encode one binary device-audio, gateway-audio, or device-video envelope.
 *
 * output_size must be at least header size + strlen(session_id) +
 * payload_length, where the header size is VOICE_SESSION_ENVELOPE_HEADER_SIZE
 * (24) for the audio magics and VOICE_SESSION_VIDEO_ENVELOPE_HEADER_SIZE (26)
 * for the device-video magic. The payload cap follows the magic: audio magics
 * accept at most VOICE_SESSION_MAX_PAYLOAD_BYTES, the device-video magic at
 * most VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES. Returns the protocol error code
 * and writes the number of bytes on success; the audio output is byte-for-byte
 * unchanged from the version 1 format.
 */
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
);

/**
 * @brief Decode and validate one binary audio or device-video envelope.
 *
 * expected_magic may be VOICE_SESSION_ENVELOPE_MAGIC_NONE to accept any known
 * magic. The version, header size, and payload length width follow the magic,
 * so a still carrying version 1 and an audio frame carrying version 2 are both
 * rejected. The payload cap is enforced per magic exactly as on encode. The
 * payload points into input and remains owned by the caller.
 */
voice_session_protocol_error_t voice_session_envelope_decode(
    const uint8_t *input,
    size_t input_length,
    voice_session_envelope_magic_t expected_magic,
    voice_session_envelope_t *envelope_out
);

#ifdef __cplusplus
}
#endif
