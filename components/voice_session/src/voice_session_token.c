#include "voice_session_token.h"

#include <string.h>

#include "cJSON.h"

// Copy one JSON string into a bounded destination. Returns false when the value
// is missing, empty, or too long to hold safely so the caller rejects the whole
// response rather than opening a socket with a truncated credential.
static bool voice_session_token_copy(
    char *destination,
    size_t destination_size,
    const char *value
) {
    if (destination == NULL || destination_size == 0 || value == NULL) {
        return false;
    }
    const size_t length = strlen(value);
    if (length == 0 || length >= destination_size) {
        return false;
    }
    memcpy(destination, value, length + 1);
    return true;
}

bool voice_session_token_parse(
    const char *body,
    size_t length,
    voice_session_token_t *token_out
) {
    if (body == NULL || token_out == NULL || length == 0) {
        return false;
    }
    // cJSON_Parse needs a NUL-terminated buffer. Reject an over-long body first
    // so a bounded copy can always hold it.
    if (length >= VOICE_SESSION_TOKEN_SIZE * 4) {
        return false;
    }
    char bounded[VOICE_SESSION_TOKEN_SIZE * 4];
    memcpy(bounded, body, length);
    bounded[length] = '\0';

    cJSON *root = cJSON_Parse(bounded);
    if (root == NULL) {
        return false;
    }
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (data == NULL || !cJSON_IsObject(data)) {
        cJSON_Delete(root);
        return false;
    }
    const char *url = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(data, "websocket_url")
    );
    const char *token = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(data, "token")
    );
    voice_session_token_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    const bool ok =
        voice_session_token_copy(
            parsed.websocket_url,
            sizeof(parsed.websocket_url),
            url
        ) &&
        voice_session_token_copy(parsed.token, sizeof(parsed.token), token);
    cJSON_Delete(root);
    if (!ok) {
        return false;
    }
    *token_out = parsed;
    return true;
}
