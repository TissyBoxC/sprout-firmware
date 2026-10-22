#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_CODEC_SAMPLE_RATE_HZ 16000
#define AUDIO_CODEC_CHANNEL_COUNT 1
#define AUDIO_CODEC_FRAME_DURATION_MS 20
#define AUDIO_CODEC_SAMPLES_PER_FRAME \
    (AUDIO_CODEC_SAMPLE_RATE_HZ * AUDIO_CODEC_FRAME_DURATION_MS / 1000)
#define AUDIO_CODEC_FRAME_PCM_BYTES \
    (AUDIO_CODEC_SAMPLES_PER_FRAME * AUDIO_CODEC_CHANNEL_COUNT * sizeof(int16_t))
#define AUDIO_CODEC_MAX_PACKET_BYTES 1024

typedef struct {
    uint32_t sequence;
    size_t pcm_size;
    int16_t pcm[AUDIO_CODEC_SAMPLES_PER_FRAME * AUDIO_CODEC_CHANNEL_COUNT];
} audio_codec_pcm_frame_t;

#ifdef __cplusplus
}
#endif
