#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_PIPELINE_CAPTURE_OWNER_NONE = 0,
    AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE,
    AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT,
} audio_pipeline_capture_owner_t;

esp_err_t audio_pipeline_capture_release(
    audio_pipeline_capture_owner_t owner
);

#ifdef __cplusplus
}
#endif
