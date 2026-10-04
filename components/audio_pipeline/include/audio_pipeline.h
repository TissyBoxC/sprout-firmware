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

/** @brief Stable logical owner of the shared microphone capture stream. */
typedef enum {
    AUDIO_PIPELINE_CAPTURE_OWNER_NONE = 0,
    AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE,
    AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT,
} audio_pipeline_capture_owner_t;

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
 * the configured timeout. The caller must identify itself with the same owner
 * that holds the capture lease; a different or ownerless reader is rejected
 * before I2S is touched so two tasks cannot interleave a 20 ms frame.
 */
audio_codec_error_t audio_pipeline_capture_frame(
    audio_pipeline_capture_owner_t owner,
    audio_codec_pcm_frame_t *frame_out
);

/**
 * @brief Atomically acquire the shared microphone capture lease.
 *
 * Exactly one owner may hold the lease. Returns ESP_ERR_INVALID_STATE when
 * another owner already holds it, and ESP_ERR_NOT_SUPPORTED when this build
 * has no input direction. Acquisition also enables the I2S capture direction.
 */
esp_err_t audio_pipeline_capture_acquire(
    audio_pipeline_capture_owner_t owner
);

/**
 * @brief Release the shared microphone capture lease.
 *
 * The caller must pass the same owner that acquired the lease. A mismatch
 * returns ESP_ERR_INVALID_STATE and leaves the current owner untouched.
 */
esp_err_t audio_pipeline_capture_release(
    audio_pipeline_capture_owner_t owner
);

/** @brief Return the current microphone capture owner. */
audio_pipeline_capture_owner_t audio_pipeline_capture_get_owner(void);

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
 * This compatibility gate still enables and disables the raw I2S direction,
 * but it does not create a capture owner. Disabling while a lease is held
 * returns ESP_ERR_INVALID_STATE; the owner must use
 * audio_pipeline_capture_release instead. Returns ESP_ERR_NOT_SUPPORTED when
 * the compiled firmware has no input direction.
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

/**
 * @brief Receive each PCM frame after it has been written to the speaker.
 *
 * The callback runs synchronously on the playback task, so it must copy any
 * samples it keeps and return quickly. It receives the exact signal the
 * loudspeaker played, which makes it usable as an acoustic echo reference.
 */
typedef void (*audio_pipeline_reference_sink_t)(
    const int16_t *samples,
    size_t sample_count,
    void *context
);

/**
 * @brief Register or clear the speaker reference sink.
 *
 * Passing a NULL sink clears the registration and ignores the context. The
 * callback is never invoked before this function returns. Registration is safe
 * while playback is active; the change takes effect at the next frame boundary.
 */
esp_err_t audio_pipeline_set_reference_sink(
    audio_pipeline_reference_sink_t sink,
    void *context
);

/** @brief Return the removable-module descriptor for audio_pipeline. */
const module_descriptor_t *audio_pipeline_module_descriptor(void);

#ifdef __cplusplus
}
#endif
