#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_audio_dec.h"

typedef enum {
    ESP_OPUS_DEC_FRAME_DURATION_20_MS = 3,
} esp_opus_dec_frame_duration_t;

typedef struct {
    uint32_t sample_rate;
    uint8_t channel;
    esp_opus_dec_frame_duration_t frame_duration;
    bool self_delimited;
} esp_opus_dec_cfg_t;

esp_audio_err_t esp_opus_dec_open(void *cfg, uint32_t cfg_sz, void **dec_handle);
esp_audio_err_t esp_opus_dec_decode(
    void *dec_handle,
    esp_audio_dec_in_raw_t *raw,
    esp_audio_dec_out_frame_t *frame,
    void *dec_info
);
esp_audio_err_t esp_opus_dec_reset(void *dec_handle);
void esp_opus_dec_close(void *dec_handle);
