#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_CODEC_SAMPLE_RATE_HZ 16000
#define AUDIO_CODEC_CHANNEL_COUNT 1
#define AUDIO_CODEC_FRAME_DURATION_MS 20
#define AUDIO_CODEC_SAMPLES_PER_FRAME 320
#define AUDIO_CODEC_MAX_PACKET_BYTES 1024
#define AUDIO_CODEC_ENCODING_NAME "opus"

typedef enum {
    AUDIO_CODEC_OK = 0,
    AUDIO_CODEC_ERR_NOT_INITIALIZED,
    AUDIO_CODEC_ERR_DECODE_FAILED,
} audio_codec_error_t;

typedef struct {
    uint32_t sequence;
    size_t pcm_size;
    int16_t pcm[AUDIO_CODEC_SAMPLES_PER_FRAME];
} audio_codec_pcm_frame_t;

typedef struct {
    uint64_t captured_at_ms;
    size_t packet_size;
    uint8_t packet[AUDIO_CODEC_MAX_PACKET_BYTES];
} audio_codec_packet_t;

bool audio_codec_is_ready(void);
audio_codec_error_t audio_codec_decode_packet(
    const uint8_t *packet,
    size_t packet_size,
    audio_codec_pcm_frame_t *frame_out
);
