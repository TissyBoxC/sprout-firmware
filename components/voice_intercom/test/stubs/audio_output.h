#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "audio_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_OUTPUT_PRIORITY_AMBIENT = 0,
    AUDIO_OUTPUT_PRIORITY_CONVERSATION,
    AUDIO_OUTPUT_PRIORITY_PROMPT,
    AUDIO_OUTPUT_PRIORITY_SAFETY,
    AUDIO_OUTPUT_PRIORITY_COUNT,
} audio_output_priority_t;

typedef enum {
    AUDIO_OUTPUT_OK = 0,
    AUDIO_OUTPUT_ERR_NOT_INITIALIZED,
    AUDIO_OUTPUT_ERR_INVALID_ARGUMENT,
    AUDIO_OUTPUT_ERR_INVALID_PCM_SIZE,
    AUDIO_OUTPUT_ERR_QUEUE_FULL,
    AUDIO_OUTPUT_ERR_DISCARDED,
    AUDIO_OUTPUT_ERR_TIMEOUT,
    AUDIO_OUTPUT_ERR_INTERNAL,
} audio_output_error_t;

audio_output_error_t audio_output_submit(
    const audio_codec_pcm_frame_t *frame,
    audio_output_priority_t priority
);
audio_output_error_t audio_output_flush(
    audio_output_priority_t max_priority,
    bool include_safety,
    size_t *discarded_frames_out
);

#ifdef __cplusplus
}
#endif
