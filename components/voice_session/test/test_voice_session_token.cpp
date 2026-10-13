#include "voice_session_token.h"

#include <cassert>
#include <cstring>

int main() {
    // A well-formed success envelope yields both fields.
    const char *valid =
        "{\"schema_version\":\"1.0.0\",\"request_id\":\"req-1\","
        "\"data\":{\"websocket_url\":\"wss://voice.example.test/v1/voice\","
        "\"token\":\"signed.voice.token\"},\"error\":null}";
    voice_session_token_t parsed;
    std::memset(&parsed, 0, sizeof(parsed));
    assert(
        voice_session_token_parse(valid, std::strlen(valid), &parsed)
    );
    assert(
        std::strcmp(parsed.websocket_url, "wss://voice.example.test/v1/voice")
        == 0
    );
    assert(std::strcmp(parsed.token, "signed.voice.token") == 0);

    // A missing data object is rejected.
    const char *no_data = "{\"schema_version\":\"1.0.0\"}";
    assert(
        !voice_session_token_parse(no_data, std::strlen(no_data), &parsed)
    );

    // A missing token is rejected rather than connecting with an empty one.
    const char *no_token =
        "{\"data\":{\"websocket_url\":\"wss://voice.example.test/v1/voice\"}}";
    assert(
        !voice_session_token_parse(no_token, std::strlen(no_token), &parsed)
    );

    // A missing URL is rejected.
    const char *no_url = "{\"data\":{\"token\":\"signed.voice.token\"}}";
    assert(!voice_session_token_parse(no_url, std::strlen(no_url), &parsed));

    // A malformed body is rejected.
    const char *malformed = "{not json";
    assert(
        !voice_session_token_parse(
            malformed,
            std::strlen(malformed),
            &parsed
        )
    );

    // An over-long token is rejected so a truncated credential never reaches
    // the gateway.
    char oversized[VOICE_SESSION_TOKEN_SIZE * 2];
    const char *prefix =
        "{\"data\":{\"websocket_url\":\"wss://voice.example.test/v1/voice\","
        "\"token\":\"";
    const char *suffix = "\"}}";
    std::strcpy(oversized, prefix);
    size_t cursor = std::strlen(prefix);
    // Fill well past the token buffer so the value is genuinely too long.
    while (cursor + std::strlen(suffix) < sizeof(oversized)) {
        oversized[cursor++] = 'a';
    }
    std::strcpy(oversized + cursor, suffix);
    cursor += std::strlen(suffix);
    assert(!voice_session_token_parse(oversized, cursor, &parsed));

    // The parser honours the supplied length instead of reading past it.
    assert(!voice_session_token_parse(valid, 8, &parsed));

    return 0;
}
