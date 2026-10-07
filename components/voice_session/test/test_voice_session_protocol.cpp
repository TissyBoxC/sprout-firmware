// Host test for the pure voice session protocol helpers.
//
// The envelope codec, identifier validation, and state mapping carry no
// ESP-IDF dependency. tools/run_host_tests.ps1 builds it together with
// src/voice_session_protocol.c so CI exercises it without hardware.

#include "voice_session_protocol.h"

#include <cassert>
#include <cstring>

static void test_identifier_validation(void) {
    assert(voice_session_identifier_is_valid("session_a1b2c3"));
    assert(voice_session_identifier_is_valid("stream_0"));
    assert(voice_session_identifier_is_valid("a_1"));
    assert(voice_session_identifier_is_valid("device_0123456789abcdef"));

    assert(!voice_session_identifier_is_valid(nullptr));
    assert(!voice_session_identifier_is_valid(""));
    assert(!voice_session_identifier_is_valid("session"));
    assert(!voice_session_identifier_is_valid("Session_a"));
    assert(!voice_session_identifier_is_valid("session_"));
    assert(!voice_session_identifier_is_valid("session__a"));
    assert(!voice_session_identifier_is_valid("session-a"));
    assert(!voice_session_identifier_is_valid("1session_a"));
    assert(!voice_session_identifier_is_valid("a_b_c_d_e_f_g_h_i"));
}

static void test_identifier_generation(void) {
    char output[VOICE_SESSION_IDENTIFIER_SIZE] = {0};
    assert(voice_session_identifier_generate(
        "session",
        0xDEADBEEF,
        output,
        sizeof(output)
    ));
    assert(std::strcmp(output, "session_deadbeef") == 0);
    assert(voice_session_identifier_is_valid(output));

    assert(!voice_session_identifier_generate(
        "Session",
        1,
        output,
        sizeof(output)
    ));
    assert(!voice_session_identifier_generate(
        "session",
        1,
        output,
        4
    ));
}

static void test_state_mapping(void) {
    voice_session_protocol_state_t state = VOICE_SESSION_PROTOCOL_STATE_IDLE;
    assert(voice_session_state_from_name("listening", &state));
    assert(state == VOICE_SESSION_PROTOCOL_STATE_LISTENING);
    assert(std::strcmp(
        voice_session_protocol_state_name(
            VOICE_SESSION_PROTOCOL_STATE_SPEAKING
        ),
        "speaking"
    ) == 0);
    assert(!voice_session_state_from_name("waiting", &state));
}

static void test_envelope_round_trip(void) {
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t encoded[256] = {0};
    size_t encoded_length = 0;
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO,
        "stream_1234abcd",
        42,
        1699999999123ULL,
        payload,
        sizeof(payload),
        encoded,
        sizeof(encoded),
        &encoded_length
    ) == VOICE_SESSION_PROTOCOL_OK);

    // Header is 24 bytes plus the session identifier, then the payload.
    assert(encoded_length == 24 + std::strlen("stream_1234abcd") + 5);
    assert(std::memcmp(encoded, "SRAW", 4) == 0);
    assert(encoded[4] == 1);
    assert(encoded[5] == 0);
    assert(encoded[6] == 0);
    assert(encoded[7] == 24 + std::strlen("stream_1234abcd"));
    assert(encoded[8] == 0 && encoded[11] == 42);
    assert(encoded[20] == 0);
    assert(encoded[21] == std::strlen("stream_1234abcd"));
    assert(encoded[22] == 0 && encoded[23] == 5);

    voice_session_envelope_t decoded;
    assert(voice_session_envelope_decode(
        encoded,
        encoded_length,
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_OK);
    assert(decoded.magic == VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO);
    assert(decoded.sequence == 42);
    assert(decoded.timestamp_millis == 1699999999123ULL);
    assert(std::strcmp(decoded.session_id, "stream_1234abcd") == 0);
    assert(decoded.payload_length == sizeof(payload));
    assert(std::memcmp(decoded.payload, payload, sizeof(payload)) == 0);
}

static void test_envelope_rejections(void) {
    const uint8_t payload[] = {0x01, 0x02};
    uint8_t encoded[256] = {0};
    size_t encoded_length = 0;
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_SERVER_AUDIO,
        "session_abc12345",
        1,
        1,
        payload,
        sizeof(payload),
        encoded,
        sizeof(encoded),
        &encoded_length
    ) == VOICE_SESSION_PROTOCOL_OK);

    voice_session_envelope_t decoded;
    // Wrong expected magic is rejected.
    assert(voice_session_envelope_decode(
        encoded,
        encoded_length,
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_MAGIC);
    // Truncated input is rejected.
    assert(voice_session_envelope_decode(
        encoded,
        10,
        VOICE_SESSION_ENVELOPE_MAGIC_NONE,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH);
    // A tampered version is rejected.
    encoded[4] = 2;
    assert(voice_session_envelope_decode(
        encoded,
        encoded_length,
        VOICE_SESSION_ENVELOPE_MAGIC_NONE,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_VERSION);
}

int main(void) {
    test_identifier_validation();
    test_identifier_generation();
    test_state_mapping();
    test_envelope_round_trip();
    test_envelope_rejections();
    return 0;
}
