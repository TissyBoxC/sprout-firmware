#pragma once

#include <stdbool.h>

#include "audio_codec.h"
#include "esp_err.h"

/* Only the fallback path is compiled in this host build: no audio_output
   mixer and no volume control, so frames go straight to the pipeline. */
bool audio_pipeline_is_ready(void);
esp_err_t audio_pipeline_set_playing(bool is_playing);
audio_codec_error_t audio_pipeline_play_frame(
    const audio_codec_pcm_frame_t *frame
);
