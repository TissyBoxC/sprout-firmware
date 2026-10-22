#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_SESSION_STATE_IDLE = 0,
    VOICE_SESSION_STATE_CONNECTING,
    VOICE_SESSION_STATE_LISTENING,
    VOICE_SESSION_STATE_THINKING,
    VOICE_SESSION_STATE_SPEAKING,
    VOICE_SESSION_STATE_CLOSED,
    VOICE_SESSION_STATE_FAILED,
} voice_session_state_t;

typedef struct {
    bool is_initialized;
    bool is_connected;
    bool has_active_session;
    voice_session_state_t state;
} voice_session_snapshot_t;

bool voice_session_is_ready(void);
esp_err_t voice_session_start_session(
    const char *stream_id,
    const char *session_id
);
esp_err_t voice_session_end_session(const char *reason);
esp_err_t voice_session_get_snapshot(voice_session_snapshot_t *snapshot_out);
void voice_session_shutdown(void);

#ifdef __cplusplus
}
#endif
