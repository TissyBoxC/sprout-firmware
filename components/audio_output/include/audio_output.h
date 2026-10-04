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
 * @brief Priority lanes mixed by the speaker output.
 *
 * Values are contiguous and ordered so callers can map the playback queue
 * priority directly without relying on enum representation.
 */
typedef enum {
    AUDIO_OUTPUT_PRIORITY_AMBIENT = 0,
    AUDIO_OUTPUT_PRIORITY_CONVERSATION,
    AUDIO_OUTPUT_PRIORITY_PROMPT,
    AUDIO_OUTPUT_PRIORITY_SAFETY,
    AUDIO_OUTPUT_PRIORITY_COUNT,
} audio_output_priority_t;

/** @brief Stable errors returned by the output mixer. */
typedef enum {
    AUDIO_OUTPUT_OK = 0,
    AUDIO_OUTPUT_ERR_NOT_INITIALIZED,
    AUDIO_OUTPUT_ERR_INVALID_ARGUMENT,
    AUDIO_OUTPUT_ERR_INVALID_PCM_SIZE,
    AUDIO_OUTPUT_ERR_QUEUE_FULL,
    AUDIO_OUTPUT_ERR_DISCARDED,
    AUDIO_OUTPUT_ERR_TIMEOUT,
    AUDIO_OUTPUT_ERR_INTERNAL,
} audio_output_error_t;

/** @brief Bounded mixer state for diagnostics and the device UI. */
typedef struct {
    bool is_ready;
    bool has_primary_source;
    bool is_muted;
    uint8_t applied_volume_percent;
    uint32_t submitted_frames;
    uint32_t mixed_frames;
    uint32_t output_frames;
    uint32_t discarded_frames;
    uint32_t queue_full_frames;
    uint32_t output_errors;
    uint32_t pending_frames[AUDIO_OUTPUT_PRIORITY_COUNT];
} audio_output_snapshot_t;

/**
 * @brief Initialize the mixer and its bounded priority lanes.
 *
 * The I2S pipeline must already be initialized. The mixer task stays idle
 * until a frame is submitted and never enables the microphone.
 */
esp_err_t audio_output_init(void);

/** @brief Return true when the mixer can accept frames. */
bool audio_output_is_ready(void);

/**
 * @brief Stop the mixer, discard every lane, and release its resources.
 *
 * Callers must stop producers first. The function waits up to two seconds for
 * the mixer task to leave the I2S write before freeing the lane buffers.
 */
void audio_output_shutdown(void);

/**
 * @brief Submit one whole PCM frame to a priority lane.
 *
 * The frame is copied before the call returns. When a lane is full the call
 * waits up to CONFIG_AUDIO_OUTPUT_SUBMIT_TIMEOUT_MS so a short network burst
 * cannot create a permanent drop.
 */
audio_output_error_t audio_output_submit(
    const audio_codec_pcm_frame_t *frame,
    audio_output_priority_t priority
);

/**
 * @brief Submit one frame and wait until the mixer has rendered it.
 *
 * This is the pacing operation used by the prioritized playback queue. It
 * returns only after the exact frame has been mixed and sent to I2S, so the
 * caller can preserve preemption and requeue accounting without guessing how
 * many frames the mixer has buffered. A stalled mixer is reported after
 * CONFIG_AUDIO_OUTPUT_RENDER_TIMEOUT_MS instead of blocking a playback worker
 * forever.
 */
audio_output_error_t audio_output_submit_blocking(
    const audio_codec_pcm_frame_t *frame,
    audio_output_priority_t priority
);

/**
 * @brief Discard queued frames at one priority.
 *
 * Safety frames are discarded only when this function is explicitly called
 * with AUDIO_OUTPUT_PRIORITY_SAFETY. The discarded count is optional and is
 * reported for playback queue accounting.
 */
audio_output_error_t audio_output_discard_priority(
    audio_output_priority_t priority,
    size_t *discarded_frames_out
);

/**
 * @brief Discard queued frames through one priority, optionally including safety.
 *
 * This is intended for session teardown and full reset. Normal prompt
 * playback should use audio_output_discard_priority so unrelated lanes are not
 * disturbed.
 */
audio_output_error_t audio_output_flush(
    audio_output_priority_t max_priority,
    bool include_safety,
    size_t *discarded_frames_out
);

/** @brief Return the current bounded mixer snapshot. */
audio_output_snapshot_t audio_output_get_snapshot(void);

/** @brief Return the stable string for one priority. */
const char *audio_output_priority_name(audio_output_priority_t priority);

/** @brief Return the stable string for one mixer error code. */
const char *audio_output_error_name(audio_output_error_t error);

/** @brief Return the removable-module descriptor for audio_output. */
const module_descriptor_t *audio_output_module_descriptor(void);

#ifdef __cplusplus
}
#endif
