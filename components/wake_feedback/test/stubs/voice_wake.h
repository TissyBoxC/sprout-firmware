#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Only the fields wake_feedback reads are modeled.
#define VOICE_WAKE_DETAIL_CODE_SIZE 64

typedef struct {
    uint32_t wake_word_id;
    char wake_word_name[32];
    char detail_code[VOICE_WAKE_DETAIL_CODE_SIZE];
    uint32_t confidence_milli;
    uint64_t detected_at_ms;
    uint32_t session_nonce;
} voice_wake_event_t;

typedef void (*voice_wake_handler_t)(
    const voice_wake_event_t *event,
    void *context
);

bool voice_wake_is_ready(void);
esp_err_t voice_wake_set_wake_handler(
    voice_wake_handler_t handler,
    void *context
);
