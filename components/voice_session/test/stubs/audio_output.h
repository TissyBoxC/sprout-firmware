#pragma once

#include "audio_codec.h"

typedef enum {
    AUDIO_OUTPUT_PRIORITY_CONVERSATION = 0,
} audio_output_priority_t;

esp_err_t audio_output_discard_priority(
    audio_output_priority_t priority,
    void *context
);
