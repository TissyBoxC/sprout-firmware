#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum length of the returned voice WebSocket URL including NUL. */
#define VOICE_SESSION_TOKEN_URL_SIZE 256

/** @brief Maximum length of the signed voice credential including NUL. */
#define VOICE_SESSION_TOKEN_SIZE 512

/** @brief One parsed voice credential returned by the platform. */
typedef struct {
    char websocket_url[VOICE_SESSION_TOKEN_URL_SIZE];
    char token[VOICE_SESSION_TOKEN_SIZE];
} voice_session_token_t;

/**
 * @brief Parse a `POST /api/v1/devices/{device_id}/voice-token` response.
 *
 * The platform replies with the shared success envelope
 * `{ "data": { "websocket_url": ..., "token": ... } }`. Both fields must be
 * present and fit their bounded buffers; a malformed body, a missing field, or
 * an over-long value is rejected so the caller never opens a socket with a
 * truncated credential or an empty endpoint. The token is never logged here.
 */
bool voice_session_token_parse(
    const char *body,
    size_t length,
    voice_session_token_t *token_out
);

#ifdef __cplusplus
}
#endif
