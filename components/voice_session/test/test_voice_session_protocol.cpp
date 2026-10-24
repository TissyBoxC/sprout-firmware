// Host test for the pure voice session protocol helpers.
//
// The envelope codec, identifier validation, and state mapping carry no
// ESP-IDF dependency. tools/run_host_tests.ps1 builds it together with
// src/voice_session_protocol.c so CI exercises it without hardware.

#include "voice_session_protocol.h"

#include <cassert>
#include <cstring>
#include <initializer_list>

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
    const uint8_t payload[] = {0x01, 0x02};    uint8_t encoded[256] = {0};
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

static void test_video_envelope_round_trip(void) {
    // A still uses its own magic, version, and 26-byte header so the payload
    // length can exceed the 16-bit field of an audio envelope.
    const uint8_t jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0xFF, 0xD9};
    const size_t session_id_length = std::strlen("session_1234abcd");
    uint8_t encoded[256] = {0};
    size_t encoded_length = 0;
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        "session_1234abcd",
        7,
        1699999999456ULL,
        jpeg,
        sizeof(jpeg),
        encoded,
        sizeof(encoded),
        &encoded_length
    ) == VOICE_SESSION_PROTOCOL_OK);

    // The wire magic must be exactly S R V D; a transposed literal or a
    // gateway mismatch would otherwise still round-trip inside the device.
    const uint8_t magic[4] = {'S', 'R', 'V', 'D'};
    assert(std::memcmp(encoded, magic, 4) == 0);
    assert(encoded[0] == 0x53);
    assert(encoded[1] == 0x52);
    assert(encoded[2] == 0x56);
    assert(encoded[3] == 0x44);
    assert(encoded[4] == VOICE_SESSION_VIDEO_ENVELOPE_VERSION);
    assert(encoded[4] == 2);
    assert(encoded[5] == 0);
    assert(encoded[6] == 0);
    assert(encoded[7] == VOICE_SESSION_VIDEO_ENVELOPE_HEADER_SIZE + session_id_length);
    assert(encoded[20] == 0);
    assert(encoded[21] == session_id_length);
    // The video payload length is a big-endian uint32 and the id starts at 26.
    assert(encoded[22] == 0);
    assert(encoded[23] == 0);
    assert(encoded[24] == 0);
    assert(encoded[25] == sizeof(jpeg));
    assert(std::memcmp(&encoded[26], "session_1234abcd", session_id_length) == 0);
    assert(std::memcmp(&encoded[26 + session_id_length], jpeg, sizeof(jpeg)) == 0);
    // 26 header bytes plus the session id plus the still, not the 24 of audio.
    assert(encoded_length == 26 + session_id_length + sizeof(jpeg));

    voice_session_envelope_t decoded;
    assert(voice_session_envelope_decode(
        encoded,
        encoded_length,
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_OK);
    assert(decoded.magic == VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO);
    assert(decoded.sequence == 7);
    assert(decoded.timestamp_millis == 1699999999456ULL);
    assert(std::strcmp(decoded.session_id, "session_1234abcd") == 0);
    assert(decoded.payload_length == sizeof(jpeg));
    assert(std::memcmp(decoded.payload, jpeg, sizeof(jpeg)) == 0);

    // A receiver expecting audio must reject a still, and the reverse.
    assert(voice_session_envelope_decode(
        encoded,
        encoded_length,
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_MAGIC);
    assert(voice_session_envelope_decode(
        encoded,
        encoded_length,
        VOICE_SESSION_ENVELOPE_MAGIC_SERVER_AUDIO,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_MAGIC);
}

static void test_audio_envelope_format_is_unchanged(void) {
    // The audio layout must stay byte-for-byte what the gateway already reads:
    // 24-byte header, version 1, uint16 payload length, session id at 24.
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    struct {
        voice_session_envelope_magic_t magic;
        uint8_t magic_byte_2;
    } const cases[] = {
        {VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO, 'A'},
        {VOICE_SESSION_ENVELOPE_MAGIC_SERVER_AUDIO, 'S'},
    };
    for (const auto &expectation : cases) {
        uint8_t encoded[64] = {0};
        size_t encoded_length = 0;
        assert(voice_session_envelope_encode(
            expectation.magic,
            "session_abc12345",
            9,
            1234567890123ULL,
            payload,
            sizeof(payload),
            encoded,
            sizeof(encoded),
            &encoded_length
        ) == VOICE_SESSION_PROTOCOL_OK);
        const size_t session_id_length = std::strlen("session_abc12345");
        assert(encoded[0] == 'S');
        assert(encoded[1] == 'R');
        assert(encoded[2] == expectation.magic_byte_2);
        assert(encoded[4] == VOICE_SESSION_ENVELOPE_VERSION);
        assert(encoded[4] == 1);
        assert(encoded[5] == 0);
        assert(encoded[6] == 0);
        assert(encoded[7] == VOICE_SESSION_ENVELOPE_HEADER_SIZE + session_id_length);
        assert(encoded[20] == 0);
        assert(encoded[21] == session_id_length);
        assert(encoded[22] == 0);
        assert(encoded[23] == sizeof(payload));
        assert(std::memcmp(&encoded[24], "session_abc12345", session_id_length) == 0);
        assert(encoded_length == 24 + session_id_length + sizeof(payload));
    }
}

static void test_envelope_version_is_per_magic(void) {
    const uint8_t payload[] = {0xFF, 0xD8};
    uint8_t video[128] = {0};
    size_t video_length = 0;
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        "session_abc12345",
        0,
        0,
        payload,
        sizeof(payload),
        video,
        sizeof(video),
        &video_length
    ) == VOICE_SESSION_PROTOCOL_OK);

    uint8_t audio[128] = {0};
    size_t audio_length = 0;
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO,
        "session_abc12345",
        0,
        0,
        payload,
        sizeof(payload),
        audio,
        sizeof(audio),
        &audio_length
    ) == VOICE_SESSION_PROTOCOL_OK);

    voice_session_envelope_t decoded;
    // A still carrying the audio version is refused.
    video[4] = (uint8_t)VOICE_SESSION_ENVELOPE_VERSION;
    assert(voice_session_envelope_decode(
        video,
        video_length,
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_VERSION);
    // An audio frame carrying the video version is refused.
    audio[4] = (uint8_t)VOICE_SESSION_VIDEO_ENVELOPE_VERSION;
    assert(voice_session_envelope_decode(
        audio,
        audio_length,
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_VERSION);
    // An unknown version is refused for either magic.
    video[4] = 3;
    assert(voice_session_envelope_decode(
        video,
        video_length,
        VOICE_SESSION_ENVELOPE_MAGIC_NONE,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_VERSION);
}

static void test_video_audio_magics_do_not_collide(void) {
    const uint8_t payload[] = {0x01};

    // Each magic keeps its own wire bytes, and the decode of one magic is not
    // accepted when another magic is expected.
    const uint8_t wire_device_audio[4] = {'S', 'R', 'A', 'W'};
    const uint8_t wire_server_audio[4] = {'S', 'R', 'S', 'V'};
    const uint8_t wire_device_video[4] = {'S', 'R', 'V', 'D'};
    struct {
        voice_session_envelope_magic_t magic;
        const uint8_t *wire;
    } const cases[] = {
        {VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO, wire_device_audio},
        {VOICE_SESSION_ENVELOPE_MAGIC_SERVER_AUDIO, wire_server_audio},
        {VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO, wire_device_video},
    };
    for (const auto &expectation : cases) {
        uint8_t encoded[64] = {0};
        size_t encoded_length = 0;
        assert(voice_session_envelope_encode(
            expectation.magic,
            "session_abc12345",
            0,
            0,
            payload,
            sizeof(payload),
            encoded,
            sizeof(encoded),
            &encoded_length
        ) == VOICE_SESSION_PROTOCOL_OK);
        assert(std::memcmp(encoded, expectation.wire, 4) == 0);
        voice_session_envelope_t decoded;
        assert(voice_session_envelope_decode(
            encoded,
            encoded_length,
            expectation.magic,
            &decoded
        ) == VOICE_SESSION_PROTOCOL_OK);
        assert(decoded.magic == expectation.magic);
    }

    // An unknown four-byte magic is still refused.
    const uint8_t wire_unknown[4] = {'S', 'R', 'X', 'X'};
    const size_t session_id_length = std::strlen("session_abc12345");
    uint8_t unknown[64] = {0};
    std::memcpy(unknown, wire_unknown, 4);
    unknown[4] = 1;
    unknown[7] = (uint8_t)(VOICE_SESSION_ENVELOPE_HEADER_SIZE + session_id_length);
    unknown[21] = (uint8_t)session_id_length;
    unknown[23] = 1;
    std::memcpy(&unknown[24], "session_abc12345", session_id_length);
    voice_session_envelope_t decoded;
    assert(voice_session_envelope_decode(
        unknown,
        24 + session_id_length + 1,
        VOICE_SESSION_ENVELOPE_MAGIC_NONE,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_MAGIC);
}

static void test_video_payload_cap(void) {
    static uint8_t payload[VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES + 1];
    static uint8_t encoded[
        VOICE_SESSION_VIDEO_ENVELOPE_HEADER_SIZE +
        VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES +
        VOICE_SESSION_IDENTIFIER_SIZE
    ];
    std::memset(payload, 0x5A, sizeof(payload));
    std::memset(encoded, 0, sizeof(encoded));
    const size_t session_id_length = std::strlen("session_abc12345");
    size_t encoded_length = 0;

    // The full video cap is accepted and the 32-bit length field matches.
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        "session_abc12345",
        1,
        1,
        payload,
        VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES,
        encoded,
        sizeof(encoded),
        &encoded_length
    ) == VOICE_SESSION_PROTOCOL_OK);
    assert(encoded_length == 26 + session_id_length +
        VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES);
    assert(encoded[22] == 0x00 && encoded[23] == 0x04);
    assert(encoded[24] == 0x00 && encoded[25] == 0x00);

    voice_session_envelope_t decoded;
    assert(voice_session_envelope_decode(
        encoded,
        encoded_length,
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_OK);
    assert(decoded.payload_length == VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES);

    // One byte over the video cap is refused on both sides.
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        "session_abc12345",
        1,
        1,
        payload,
        VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES + 1,
        encoded,
        sizeof(encoded),
        &encoded_length
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH);
    encoded[22] = 0x00;
    encoded[23] = 0x04;
    encoded[24] = 0x00;
    encoded[25] = 0x01;
    assert(voice_session_envelope_decode(
        encoded,
        encoded_length + 1,
        VOICE_SESSION_ENVELOPE_MAGIC_NONE,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH);

    // The audio cap is unchanged: the video limit must not widen it.
    assert(VOICE_SESSION_MAX_PAYLOAD_BYTES == 1024);
    assert(VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES == 262144);
    for (voice_session_envelope_magic_t magic : {
             VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO,
             VOICE_SESSION_ENVELOPE_MAGIC_SERVER_AUDIO,
         }) {
        assert(voice_session_envelope_encode(
            magic,
            "session_abc12345",
            1,
            1,
            payload,
            VOICE_SESSION_MAX_PAYLOAD_BYTES,
            encoded,
            sizeof(encoded),
            &encoded_length
        ) == VOICE_SESSION_PROTOCOL_OK);
        assert(voice_session_envelope_encode(
            magic,
            "session_abc12345",
            1,
            1,
            payload,
            VOICE_SESSION_MAX_PAYLOAD_BYTES + 1,
            encoded,
            sizeof(encoded),
            &encoded_length
        ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH);
    }

    // A still must fit the output buffer exactly; a short buffer is refused.
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        "session_abc12345",
        1,
        1,
        payload,
        1024,
        encoded,
        26 + session_id_length + 1023,
        &encoded_length
    ) == VOICE_SESSION_PROTOCOL_ERR_OUTPUT_TOO_SMALL);
}

static void test_video_envelope_rejections(void) {
    const uint8_t payload[] = {0xFF, 0xD8};
    const size_t session_id_length = std::strlen("session_abc12345");
    uint8_t encoded[64] = {0};
    size_t encoded_length = 0;

    // The still path shares the argument rules of the audio path.
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        "session_abc12345",
        0,
        0,
        payload,
        0,
        encoded,
        sizeof(encoded),
        &encoded_length
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH);
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        "session",
        0,
        0,
        payload,
        sizeof(payload),
        encoded,
        sizeof(encoded),
        &encoded_length
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_IDENTIFIER);
    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        "session_abc12345",
        0,
        0,
        nullptr,
        sizeof(payload),
        encoded,
        sizeof(encoded),
        &encoded_length
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_ARGUMENT);

    assert(voice_session_envelope_encode(
        VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_VIDEO,
        "session_abc12345",
        0,
        0,
        payload,
        sizeof(payload),
        encoded,
        sizeof(encoded),
        &encoded_length
    ) == VOICE_SESSION_PROTOCOL_OK);

    voice_session_envelope_t decoded;
    // A still shorter than the 26-byte video header is truncated.
    assert(voice_session_envelope_decode(
        encoded,
        25,
        VOICE_SESSION_ENVELOPE_MAGIC_NONE,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH);

    // A header length that does not match the session id is refused, so a
    // still cannot be decoded with the payload shifted.
    encoded[6] = 0;
    encoded[7] = (uint8_t)(VOICE_SESSION_VIDEO_ENVELOPE_HEADER_SIZE +
        session_id_length + 1);
    assert(voice_session_envelope_decode(
        encoded,
        encoded_length,
        VOICE_SESSION_ENVELOPE_MAGIC_NONE,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH);

    // The video header length field is the 26-byte layout; a 24-byte header
    // length from the audio layout is refused for a still.
    encoded[7] = (uint8_t)(VOICE_SESSION_ENVELOPE_HEADER_SIZE + session_id_length);
    assert(voice_session_envelope_decode(
        encoded,
        encoded_length,
        VOICE_SESSION_ENVELOPE_MAGIC_NONE,
        &decoded
    ) == VOICE_SESSION_PROTOCOL_ERR_INVALID_LENGTH);
}

int main(void) {
    test_identifier_validation();
    test_identifier_generation();
    test_state_mapping();
    test_envelope_round_trip();
    test_envelope_rejections();
    test_video_envelope_round_trip();
    test_audio_envelope_format_is_unchanged();
    test_envelope_version_is_per_magic();
    test_video_audio_magics_do_not_collide();
    test_video_payload_cap();
    test_video_envelope_rejections();
    return 0;
}
