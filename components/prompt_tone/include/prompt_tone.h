#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Built-in system cues.
 *
 * Every cue is generated locally from a bounded PCM envelope, so the device
 * never downloads audio just to acknowledge a wake word, report an error, or
 * announce a safety message. The set is stable because the parent application
 * maps the same identifiers to explanatory text.
 */
typedef enum {
    PROMPT_TONE_WAKE_ACCEPTED = 0,
    PROMPT_TONE_CAPTURE_STARTED,
    PROMPT_TONE_CAPTURE_STOPPED,
    PROMPT_TONE_MICROPHONE_MUTED,
    PROMPT_TONE_VOLUME_LIMIT,
    PROMPT_TONE_NETWORK_LOST,
    PROMPT_TONE_NETWORK_RESTORED,
    PROMPT_TONE_BATTERY_LOW,
    PROMPT_TONE_SAFETY_ANNOUNCEMENT,
    PROMPT_TONE_FACTORY_RESET_ARMED,
    PROMPT_TONE_FACTORY_RESET_CANCELLED,
    PROMPT_TONE_FACTORY_RESET_COMPLETED,
    PROMPT_TONE_COUNT,
} prompt_tone_t;

/** @brief Stable errors returned by the prompt module. */
typedef enum {
    PROMPT_TONE_OK = 0,
    PROMPT_TONE_ERR_NOT_INITIALIZED,
    PROMPT_TONE_ERR_INVALID_TONE,
    PROMPT_TONE_ERR_QUEUE,
} prompt_tone_error_t;

/** @brief Bounded counters for diagnostics. */
typedef struct {
    unsigned int tones_played;
    unsigned int tones_rejected;
    unsigned int safety_tones_played;
} prompt_tone_snapshot_t;

/**
 * @brief Initialize the prompt tone generator.
 *
 * The module renders tones on demand; it allocates no audio buffer at startup
 * and holds no global hardware state, so it can be removed independently.
 */
esp_err_t prompt_tone_init(void);

/** @brief Return true when the module is ready. */
bool prompt_tone_is_ready(void);

/**
 * @brief Queue one built-in cue for playback.
 *
 * Cues use the playback queue when it is present so they respect the same
 * priority and interruption rules as spoken audio. Safety announcements are
 * queued at `PLAYBACK_PRIORITY_SAFETY` and therefore cannot be preempted or
 * silenced by mute. Without a playback queue the function renders the cue
 * directly, which keeps a minimal build able to acknowledge user actions.
 */
prompt_tone_error_t prompt_tone_play(prompt_tone_t tone);

/** @brief Return the bounded tone snapshot. */
prompt_tone_snapshot_t prompt_tone_get_snapshot(void);

/** @brief Return the stable contract identifier for one tone. */
const char *prompt_tone_name(prompt_tone_t tone);

/** @brief Return the stable string for one prompt tone error code. */
const char *prompt_tone_error_name(prompt_tone_error_t error);

/** @brief Return the removable-module descriptor for prompt_tone. */
const module_descriptor_t *prompt_tone_module_descriptor(void);

#ifdef __cplusplus
}
#endif
