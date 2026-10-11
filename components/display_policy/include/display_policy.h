#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable policy schema version shared with the platform contract. */
#define DISPLAY_POLICY_SCHEMA_VERSION 1u

/** @brief Brightness bounds in percent. */
#define DISPLAY_POLICY_MIN_BRIGHTNESS 5u
#define DISPLAY_POLICY_MAX_BRIGHTNESS 100u

/** @brief Colour temperature bounds in kelvin. */
#define DISPLAY_POLICY_MIN_COLOR_TEMPERATURE 2700u
#define DISPLAY_POLICY_MAX_COLOR_TEMPERATURE 6500u

/** @brief Bounds for the viewing-distance reminder. */
#define DISPLAY_POLICY_MIN_VIEW_DISTANCE_CM 20u
#define DISPLAY_POLICY_MAX_VIEW_DISTANCE_CM 120u

/** @brief Bounds for the guardian daily screen-time cap in minutes. */
#define DISPLAY_POLICY_MIN_DAILY_LIMIT_MINUTES 5u
#define DISPLAY_POLICY_MAX_DAILY_LIMIT_MINUTES 600u

/** @brief Bounds for the continuous-use reminder interval in minutes. */
#define DISPLAY_POLICY_MIN_BREAK_MINUTES 5u
#define DISPLAY_POLICY_MAX_BREAK_MINUTES 240u

/** @brief Bounds for the auto-dim idle window in seconds. */
#define DISPLAY_POLICY_MIN_DIM_AFTER_SECONDS 10u
#define DISPLAY_POLICY_MAX_DIM_AFTER_SECONDS 600u

/** @brief One validated eye-care policy applied by the display driver. */
typedef struct {
    uint32_t schema_version;
    uint8_t brightness_percent;
    uint16_t color_temperature_kelvin;
    uint16_t min_view_distance_cm;
    uint16_t daily_limit_minutes;
    uint16_t break_interval_minutes;
    uint16_t dim_after_seconds;
    uint8_t auto_brightness;
    uint8_t night_light;
    uint8_t distance_reminder;
} display_policy_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    DISPLAY_POLICY_OK = 0,
    DISPLAY_POLICY_ERR_NULL_ARGUMENT,
    DISPLAY_POLICY_ERR_INVALID_POLICY,
    DISPLAY_POLICY_ERR_STORAGE,
} display_policy_error_t;

/** @brief Return the conservative default policy. */
display_policy_t display_policy_default(void);

/** @brief Clamp one policy into the documented bounds. */
display_policy_t display_policy_clamp(const display_policy_t *policy);

/** @brief Return true when a policy is version-compatible and bounded. */
bool display_policy_is_valid(const display_policy_t *policy);

/** @brief Return the stable string for one display policy error code. */
const char *display_policy_error_name(display_policy_error_t error);

/** @brief Apply the cached policy to the display hardware. */
esp_err_t display_policy_apply(const display_policy_t *policy);

/** @brief Initialize the display policy store from the configuration store. */
esp_err_t display_policy_init(void);

/** @brief Return true when the display policy store is ready. */
bool display_policy_is_ready(void);

/** @brief Store a guardian policy, clamping out-of-range values. */
esp_err_t display_policy_set(const display_policy_t *policy);

/** @brief Return the currently applied policy. */
display_policy_t display_policy_get(void);

/**
 * @brief Record that the screen woke from a dim or blank state.
 *
 * Returns the continuous-use seconds accumulated since the last reset so the
 * caller can schedule a break reminder without keeping its own timer.
 */
uint32_t display_policy_report_activity(uint32_t now_epoch_seconds);

/** @brief Start a fresh continuous-use window. */
void display_policy_reset_usage(uint32_t now_epoch_seconds);

/** @brief Return true when the break reminder interval has elapsed. */
bool display_policy_break_due(uint32_t now_epoch_seconds);

/** @brief Return true when the daily limit has been reached. */
bool display_policy_daily_limit_reached(
    uint32_t used_seconds_today,
    uint32_t now_epoch_seconds
);

/** @brief Return the removable-module descriptor for display_policy. */
const module_descriptor_t *display_policy_module_descriptor(void);

#ifdef __cplusplus
}
#endif
