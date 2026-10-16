#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_audio_types.h"

typedef enum {
    ESP_AUDIO_DEC_RECOVERY_NONE = 0,
    ESP_AUDIO_DEC_RECOVERY_PLC = 1,
} esp_audio_dec_recovery_t;

typedef struct {
    uint8_t *buffer;
    uint32_t len;
    uint32_t consumed;
    esp_audio_dec_recovery_t frame_recover;
} esp_audio_dec_in_raw_t;

typedef struct {
    uint8_t *buffer;
    uint32_t len;
    uint32_t needed_size;
    uint32_t decoded_size;
} esp_audio_dec_out_frame_t;
