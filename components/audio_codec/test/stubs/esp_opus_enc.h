#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_audio_enc.h"

/* Field order mirrors the managed component: the firmware uses designated
   initialisers, which must follow declaration order. */
typedef enum {
    ESP_OPUS_ENC_FRAME_DURATION_20_MS = 3,
} esp_opus_enc_frame_duration_t;

typedef enum {
    ESP_OPUS_ENC_APPLICATION_VOIP = 0,
    ESP_OPUS_ENC_APPLICATION_AUDIO = 1,
} esp_opus_enc_application_t;

typedef struct {
    int sample_rate;
    int channel;
    int bits_per_sample;
    int bitrate;
    esp_opus_enc_frame_duration_t frame_duration;
    esp_opus_enc_application_t application_mode;
    int complexity;
    bool enable_fec;
    bool enable_dtx;
    bool enable_vbr;
} esp_opus_enc_config_t;

esp_audio_err_t esp_opus_enc_open(void *cfg, uint32_t cfg_sz, void **enc_hd);
esp_audio_err_t esp_opus_enc_process(
    void *enc_hd,
    esp_audio_enc_in_frame_t *in_frame,
    esp_audio_enc_out_frame_t *out_frame
);
esp_audio_err_t esp_opus_enc_reset(void *enc_hd);
void esp_opus_enc_close(void *enc_hd);
