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

/** @brief Direction of one I2S transfer. */
typedef enum {
    AUDIO_PIPELINE_DIRECTION_INPUT = 0,
    AUDIO_PIPELINE_DIRECTION_OUTPUT,
} audio_pipeline_direction_t;

/** @brief Runtime state of the I2S hardware. */
typedef enum {
    AUDIO_PIPELINE_STATE_STOPPED = 0,
    AUDIO_PIPELINE_STATE_CAPTURING,
    AUDIO_PIPELINE_STATE_PLAYING,
    AUDIO_PIPELINE_STATE_ERROR,
} audio_pipeline_state_t;

/** @brief Bounded counters for telemetry and diagnostics. */
typedef struct {
    audio_pipeline_state_t state;
    unsigned int captured_frames;
    unsigned int played_frames;
    unsigned int capture_errors;
    unsigned int playback_errors;
    bool is_microphone_enabled;
    bool is_speaker_enabled;
} audio_pipeline_snapshot_t;

/**
 * @brief Initialize the I2S controller and both directions.
 *
 * The microphone starts disabled. Guardians and wake detection enable it
 * explicitly, so the device never records in the background.
 */
esp_err_t audio_pipeline_init(void);

/** @brief Return true when at least one direction is ready. */
bool audio_pipeline_is_ready(void);

/**
 * @brief Read exactly one 20 ms PCM frame from the microphone.
 *
 * Returns ESP_ERR_TIMEOUT when the DMA does not deliver a full frame before
 * the configured timeout. The caller must check audio_pipeline_is_capturing()
 * before retrying so a disabled microphone never spins.
 */
audio_codec_error_t audio_pipeline_capture_frame(
    audio_codec_pcm_frame_t *frame_out
);

/**
 * @brief Write exactly one 20 ms PCM frame to the speaker.
 *
 * The function blocks until the DMA queue accepts the frame or the configured
 * timeout expires, which bounds back pressure when the network stalls.
 */
audio_codec_error_t audio_pipeline_play_frame(
    const audio_codec_pcm_frame_t *frame
);

/**
 * @brief Enable or disable microphone capture.
 *
 * Disabling releases the capture direction so a muted device draws no
 * microphone power. Returns ESP_ERR_NOT_SUPPORTED when the compiled firmware
 * has no input direction.
 */
esp_err_t audio_pipeline_set_capturing(bool is_capturing);

/**
 * @brief Enable or disable speaker playback.
 *
 * Disabling stops the playback direction; the pipeline refuses to play until
 * it is enabled again.
 */
esp_err_t audio_pipeline_set_playing(bool is_playing);

/** @brief Return true when the microphone direction is active. */
bool audio_pipeline_is_capturing(void);

/** @brief Return true when the speaker direction is active. */
bool audio_pipeline_is_playing(void);

/** @brief Return the bounded pipeline snapshot. */
audio_pipeline_snapshot_t audio_pipeline_get_snapshot(void);

/** @brief Return the removable-module descriptor for audio_pipeline. */
const module_descriptor_t *audio_pipeline_module_descriptor(void);

#ifdef __cplusplus
}
#endif
