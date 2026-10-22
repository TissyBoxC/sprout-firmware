#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_codec.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool is_running;
    uint32_t captured_frames;
    uint32_t encoded_frames;
    uint32_t speech_frames;
    uint32_t suppressed_frames;
    uint32_t dropped_frames;
    uint32_t double_talk_frames;
    uint32_t aec_convergence_q10;
    uint32_t agc_gain_q8;
    uint16_t microphone_level_q15;
    uint16_t reference_level_q15;
} audio_input_snapshot_t;

esp_err_t audio_input_stop(void);
esp_err_t audio_input_get_snapshot(audio_input_snapshot_t *snapshot_out);

#ifdef __cplusplus
}
#endif
