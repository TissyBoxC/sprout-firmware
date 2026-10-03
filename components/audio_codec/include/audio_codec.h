#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Realtime audio profile shared with the voice gateway contract. */
#define AUDIO_CODEC_SCHEMA_VERSION "1.0.0"
#define AUDIO_CODEC_SAMPLE_RATE_HZ 16000
#define AUDIO_CODEC_CHANNEL_COUNT 1
#define AUDIO_CODEC_FRAME_DURATION_MS 20
#define AUDIO_CODEC_ENCODING_NAME "opus"

/** @brief Decoded sample count per channel for one frame. */
#define AUDIO_CODEC_SAMPLES_PER_FRAME \
    (AUDIO_CODEC_SAMPLE_RATE_HZ * AUDIO_CODEC_FRAME_DURATION_MS / 1000)

/**
 * @brief Decoded PCM bytes for one frame.
 *
 * The microphone and speaker paths both use signed 16-bit little-endian
 * samples, which matches the I2S peripheral configuration.
 */
#define AUDIO_CODEC_FRAME_PCM_BYTES \
    (AUDIO_CODEC_SAMPLES_PER_FRAME * AUDIO_CODEC_CHANNEL_COUNT * sizeof(int16_t))

/**
 * @brief Maximum encoded Opus packet size accepted from the network.
 *
 * A 20 ms wideband packet stays far below this bound, so a larger value means
 * a malformed or hostile frame and must be rejected before decoding.
 */
#define AUDIO_CODEC_MAX_PACKET_BYTES 1024

/** @brief Stable audio error codes shared with device diagnostics. */
typedef enum {
    AUDIO_CODEC_OK = 0,
    AUDIO_CODEC_ERR_NOT_INITIALIZED,
    AUDIO_CODEC_ERR_INVALID_ARGUMENT,
    AUDIO_CODEC_ERR_INVALID_PACKET_SIZE,
    AUDIO_CODEC_ERR_INVALID_PCM_SIZE,
    AUDIO_CODEC_ERR_ENCODE_FAILED,
    AUDIO_CODEC_ERR_DECODE_FAILED,
    AUDIO_CODEC_ERR_OUTPUT_TOO_SMALL,
    AUDIO_CODEC_ERR_INTERNAL,
} audio_codec_error_t;

/** @brief One encoded Opus packet with capture metadata. */
typedef struct {
    uint32_t stream_id;
    uint32_t sequence;
    uint64_t captured_at_ms;
    size_t packet_size;
    uint8_t packet[AUDIO_CODEC_MAX_PACKET_BYTES];
} audio_codec_packet_t;

/** @brief One decoded PCM frame ready for playback. */
typedef struct {
    uint32_t sequence;
    size_t pcm_size;
    int16_t pcm[AUDIO_CODEC_SAMPLES_PER_FRAME * AUDIO_CODEC_CHANNEL_COUNT];
} audio_codec_pcm_frame_t;

/** @brief Initialize the Opus encoder and decoder for the fixed profile. */
esp_err_t audio_codec_init(void);

/** @brief Return true when both codec directions are ready. */
bool audio_codec_is_ready(void);

/**
 * @brief Encode one 20 ms PCM frame into an Opus packet.
 *
 * pcm_bytes must equal AUDIO_CODEC_FRAME_PCM_BYTES. On success packet_out
 * carries the encoded packet and its size; the caller owns the structure.
 */
audio_codec_error_t audio_codec_encode_frame(
    const int16_t *pcm,
    size_t pcm_sample_count,
    audio_codec_packet_t *packet_out
);

/**
 * @brief Decode one Opus packet into a PCM frame.
 *
 * An empty packet requests Opus packet-loss concealment so a dropped network
 * frame does not break the playback timeline.
 */
audio_codec_error_t audio_codec_decode_packet(
    const uint8_t *packet,
    size_t packet_size,
    audio_codec_pcm_frame_t *frame_out
);

/**
 * @brief Clear codec prediction state between conversations.
 *
 * Opus carries prediction state across packets, so a new conversation must
 * start from a clean encoder and decoder.
 */
audio_codec_error_t audio_codec_reset(void);

/** @brief Return the stable string for one audio error code. */
const char *audio_codec_error_name(audio_codec_error_t error);

/** @brief Return the removable-module descriptor for audio_codec. */
const module_descriptor_t *audio_codec_module_descriptor(void);

#ifdef __cplusplus
}
#endif
