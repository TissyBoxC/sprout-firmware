#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define VOICE_WAKE_WORD_NAME_SIZE 32
#define VOICE_WAKE_DETAIL_CODE_SIZE 64

typedef struct {
    uint32_t wake_word_id;
    char wake_word_name[VOICE_WAKE_WORD_NAME_SIZE];
    char detail_code[VOICE_WAKE_DETAIL_CODE_SIZE];
    uint32_t confidence_milli;
    uint64_t detected_at_ms;
    uint32_t session_nonce;
} voice_wake_event_t;

typedef struct {
    bool is_ready;
    bool is_armed;
    bool is_suspended;
    uint32_t frames_processed;
    uint32_t detections;
    uint32_t rejected_frames;
    uint32_t false_wake_rejections;
    uint32_t last_confidence_milli;
    uint64_t last_detected_at_ms;
    uint32_t observation_sequence;
    voice_wake_event_t latest_event;
} voice_wake_snapshot_t;

esp_err_t voice_wake_get_snapshot(voice_wake_snapshot_t *snapshot_out);
esp_err_t voice_wake_suspend(void);
esp_err_t voice_wake_resume(void);
