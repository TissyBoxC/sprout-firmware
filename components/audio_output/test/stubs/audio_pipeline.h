#pragma once

#include <stdbool.h>

#include "audio_codec.h"
#include "esp_err.h"

bool audio_pipeline_is_ready(void);
bool audio_pipeline_is_playing(void);
esp_err_t audio_pipeline_set_playing(bool playing);
audio_codec_error_t audio_pipeline_play_frame(
    const audio_codec_pcm_frame_t *frame
);
