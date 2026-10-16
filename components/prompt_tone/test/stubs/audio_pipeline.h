#pragma once

#include <stdbool.h>

#include "audio_codec.h"
#include "esp_err.h"

/* Only the direct-to-speaker fallback surface is needed by this host build;
   the playback queue and mixer paths stay compiled out. */
bool audio_pipeline_is_ready(void);
esp_err_t audio_pipeline_set_playing(bool is_playing);
audio_codec_error_t audio_pipeline_play_frame(
    const audio_codec_pcm_frame_t *frame
);
