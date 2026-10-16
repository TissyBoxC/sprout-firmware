#pragma once

#include <stdint.h>

#include "esp_audio_types.h"

typedef struct {
    uint8_t *buffer;
    uint32_t len;
} esp_audio_enc_in_frame_t;

typedef struct {
    uint8_t *buffer;
    uint32_t len;
    uint32_t encoded_bytes;
    uint64_t pts;
} esp_audio_enc_out_frame_t;
