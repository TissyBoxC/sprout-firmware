#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_codec.h"
#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Receives one encoded speech packet from the capture task.
 *
 * The callback runs synchronously on the audio capture task, so it must copy
 * any packet it retains and return quickly. Codec packet buffers are owned by
 * the module and are only valid for the duration of the call.
 */
typedef void (*audio_input_frame_callback_t)(
    const audio_codec_packet_t *packet,
    void *context
);

/** @brief Bounded audio input counters for diagnostics and telemetry. */
typedef struct {
    bool is_running;
    uint32_t captured_frames;
    uint32_t encoded_frames;
    uint32_t speech_frames;
    uint32_t suppressed_frames;
    uint32_t dropped_frames;
    uint32_t double_talk_frames;
    /** Acoustic echo canceller convergence on a 0..1024 scale. */
    uint32_t aec_convergence_q10;
    /** Applied gain calibration on a Q8 scale; 256 means unity gain. */
    uint32_t agc_gain_q8;
} audio_input_snapshot_t;

/**
 * @brief Initialize DSP state and resources without enabling the microphone.
 *
 * Safe to call more than once. The microphone stays off until
 * audio_input_start, so a device never records in the background.
 */
esp_err_t audio_input_init(void);

/**
 * @brief Start capture for one conversation stream.
 *
 * stream_id identifies the conversation, and callback receives encoded speech
 * packets synchronously from the capture task. Non-speech frames are dropped
 * and counted, and a short pre-roll keeps the onset of speech. The callback
 * must not call any audio_input lifecycle function.
 */
esp_err_t audio_input_start(
    uint32_t stream_id,
    audio_input_frame_callback_t callback,
    void *context
);

/**
 * @brief Stop capture, release the microphone, and clear the reference sink.
 *
 * Returns ESP_ERR_TIMEOUT when the capture task does not stop within one
 * second; the caller must then treat the module as unusable and reset it.
 */
esp_err_t audio_input_stop(void);

/** @brief Copy the current bounded counters; safe to call while running. */
esp_err_t audio_input_get_snapshot(audio_input_snapshot_t *snapshot_out);

/** @brief Release every resource owned by the module and stop capture. */
void audio_input_shutdown(void);

/** @brief Return the removable-module descriptor for audio_input. */
const module_descriptor_t *audio_input_module_descriptor(void);

#ifdef __cplusplus
}
#endif
