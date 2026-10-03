#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Volume percent range shared with the guardian policy contract. */
#define VOLUME_CONTROL_PERCENT_MIN 0
#define VOLUME_CONTROL_PERCENT_MAX 100

/** @brief Stable errors returned by the volume module. */
typedef enum {
    VOLUME_CONTROL_OK = 0,
    VOLUME_CONTROL_ERR_NOT_INITIALIZED,
    VOLUME_CONTROL_ERR_INVALID_PERCENT,
    VOLUME_CONTROL_ERR_STORAGE,
} volume_control_error_t;

/** @brief Bounded volume state for telemetry and the device UI. */
typedef struct {
    uint8_t volume_percent;
    uint8_t max_volume_percent;
    bool is_muted;
} volume_control_snapshot_t;

/**
 * @brief Initialize volume state from the last persisted value.
 *
 * The guardian policy default caps the volume, and a persisted value above the
 * cap is reduced instead of trusted. A first boot without a stored value uses
 * CONFIG_VOLUME_CONTROL_DEFAULT_PERCENT bounded by the configured maximum.
 */
esp_err_t volume_control_init(void);

/** @brief Return true when the module is ready. */
bool volume_control_is_ready(void);

/**
 * @brief Set the output volume.
 *
 * A value above the guardian maximum is clamped to that maximum, matching the
 * gateway playback queue so policy enforcement is consistent on both sides.
 * Values outside 0-100 are rejected with VOLUME_CONTROL_ERR_INVALID_PERCENT.
 */
volume_control_error_t volume_control_set_percent(uint8_t volume_percent);

/**
 * @brief Apply an updated guardian maximum.
 *
 * Lowering the maximum immediately reduces the active volume, so a policy
 * change takes effect without waiting for the next conversation.
 */
volume_control_error_t volume_control_set_max_percent(uint8_t max_volume_percent);

/** @brief Return the effective volume with mute applied. */
uint8_t volume_control_get_percent(void);

/** @brief Return the guardian maximum in percent. */
uint8_t volume_control_get_max_percent(void);

/**
 * @brief Mute or unmute the speaker.
 *
 * Mute forces the effective volume to zero. It is an explicit local action and
 * never disables a safety announcement that the platform already scheduled.
 */
esp_err_t volume_control_set_muted(bool is_muted);

/** @brief Return true when the speaker is muted. */
bool volume_control_is_muted(void);

/** @brief Return the bounded volume snapshot. */
volume_control_snapshot_t volume_control_get_snapshot(void);

/** @brief Return the stable string for one volume error code. */
const char *volume_control_error_name(volume_control_error_t error);

/**
 * @brief Scale one PCM frame in place to the effective volume.
 *
 * The scaling is linear on signed 16-bit samples and saturates rather than
 * wrapping, which prevents a loud passage from turning into noise. Mute zeroes
 * the frame. Callers pass the decoded frame that is about to be written to I2S.
 */
esp_err_t volume_control_apply_gain(int16_t *pcm_samples, size_t sample_count);

/** @brief Return the removable-module descriptor for volume_control. */
const module_descriptor_t *volume_control_module_descriptor(void);

#ifdef __cplusplus
}
#endif
