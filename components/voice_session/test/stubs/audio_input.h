#pragma once

#include "audio_codec.h"

typedef void (*audio_input_frame_callback_t)(
    const audio_codec_packet_t *packet,
    void *context
);

typedef struct {
    uint16_t microphone_level_q15;
    uint16_t reference_level_q15;
    bool is_speech;
    bool has_reference;
} audio_input_frame_metrics_t;

typedef void (*audio_input_metrics_callback_t)(
    const audio_input_frame_metrics_t *metrics,
    void *context
);

typedef struct {
    bool is_running;
    uint32_t captured_frames;
    uint32_t speech_frames;
    uint32_t suppressed_frames;
    uint32_t double_talk_frames;
    uint32_t aec_convergence_q10;
    uint32_t agc_gain_q8;
    uint16_t microphone_level_q15;
    uint16_t reference_level_q15;
} audio_input_snapshot_t;

esp_err_t audio_input_start(
    uint32_t stream_id,
    audio_input_frame_callback_t callback,
    void *context
);
esp_err_t audio_input_start_with_metrics(
    uint32_t stream_id,
    audio_input_frame_callback_t callback,
    audio_input_metrics_callback_t metrics_callback,
    void *context
);
esp_err_t audio_input_stop(void);
esp_err_t audio_input_get_snapshot(audio_input_snapshot_t *snapshot_out);
