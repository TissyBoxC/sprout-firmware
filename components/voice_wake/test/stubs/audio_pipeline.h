#pragma once

#include <stdbool.h>

#include "audio_codec.h"
#include "esp_err.h"

typedef enum {
    AUDIO_PIPELINE_CAPTURE_OWNER_NONE = 0,
    AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE,
    AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT,
} audio_pipeline_capture_owner_t;

bool audio_pipeline_is_ready(void);
bool audio_pipeline_is_capturing(void);
bool audio_pipeline_is_playing(void);
esp_err_t audio_pipeline_capture_acquire(
    audio_pipeline_capture_owner_t owner
);
esp_err_t audio_pipeline_capture_release(
    audio_pipeline_capture_owner_t owner
);
audio_codec_error_t audio_pipeline_capture_frame(
    audio_pipeline_capture_owner_t owner,
    audio_codec_pcm_frame_t *frame_out
);
