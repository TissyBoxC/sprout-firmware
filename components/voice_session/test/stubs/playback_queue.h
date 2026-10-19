#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_codec.h"

#define PLAYBACK_QUEUE_ITEM_ID_SIZE 48
#define PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM 32

typedef enum {
    PLAYBACK_PRIORITY_AMBIENT = 0,
    PLAYBACK_PRIORITY_CONVERSATION,
    PLAYBACK_PRIORITY_PROMPT,
    PLAYBACK_PRIORITY_SAFETY,
    PLAYBACK_PRIORITY_COUNT,
} playback_priority_t;

typedef enum {
    PLAYBACK_QUEUE_OK = 0,
} playback_queue_error_t;

typedef struct {
    char item_id[PLAYBACK_QUEUE_ITEM_ID_SIZE];
    playback_priority_t priority;
    bool is_interruptible;
    size_t frame_count;
    audio_codec_pcm_frame_t frames[PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM];
} playback_queue_item_t;

bool playback_queue_is_ready(void);
playback_queue_error_t playback_queue_enqueue(
    const playback_queue_item_t *item
);
esp_err_t playback_queue_clear(bool include_safety);
