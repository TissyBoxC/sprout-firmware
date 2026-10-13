#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable touch configuration schema version shared with the platform. */
#define TOUCH_INPUT_SCHEMA_VERSION 1u

/** @brief Rotation values accepted by the touch controller. */
#define TOUCH_INPUT_ROTATION_0 0u
#define TOUCH_INPUT_ROTATION_90 90u
#define TOUCH_INPUT_ROTATION_180 180u
#define TOUCH_INPUT_ROTATION_270 270u

/** @brief Bounds for the tap sensitivity (0 = least, 100 = most responsive). */
#define TOUCH_INPUT_MIN_SENSITIVITY 10u
#define TOUCH_INPUT_MAX_SENSITIVITY 100u

/** @brief Bounds for the long-press threshold in milliseconds. */
#define TOUCH_INPUT_MIN_LONG_PRESS_MS 250u
#define TOUCH_INPUT_MAX_LONG_PRESS_MS 3000u

/** @brief Bounds for the double-tap window in milliseconds. */
#define TOUCH_INPUT_MIN_DOUBLE_TAP_MS 120u
#define TOUCH_INPUT_MAX_DOUBLE_TAP_MS 600u

/** @brief Bounds for the swipe distance threshold in pixels. */
#define TOUCH_INPUT_MIN_SWIPE_DISTANCE_PX 16u
#define TOUCH_INPUT_MAX_SWIPE_DISTANCE_PX 480u

/**
 * @brief One validated touch configuration.
 *
 * The configuration is deliberately small and BLE-free: it only carries the
 * gesture thresholds the driver applies and the guardian toggles the input
 * layer honours. Anything a device cannot execute is clamped away before it is
 * stored.
 */
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t tap_enabled;
    uint8_t swipe_enabled;
    uint8_t long_press_enabled;
    uint8_t haptic_enabled;
    uint8_t sensitivity;
    uint16_t rotation_degrees;
    uint16_t long_press_ms;
    uint16_t double_tap_ms;
    uint16_t swipe_distance_px;
    uint32_t revision;
} touch_input_config_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    TOUCH_INPUT_OK = 0,
    TOUCH_INPUT_ERR_NULL_ARGUMENT,
    TOUCH_INPUT_ERR_INVALID_CONFIG,
    TOUCH_INPUT_ERR_STORAGE,
} touch_input_error_t;

/** @brief Gesture classes recognised by the input layer. */
typedef enum {
    TOUCH_GESTURE_NONE = 0,
    TOUCH_GESTURE_TAP,
    TOUCH_GESTURE_DOUBLE_TAP,
    TOUCH_GESTURE_LONG_PRESS,
    TOUCH_GESTURE_SWIPE_LEFT,
    TOUCH_GESTURE_SWIPE_RIGHT,
    TOUCH_GESTURE_SWIPE_UP,
    TOUCH_GESTURE_SWIPE_DOWN,
} touch_gesture_t;

/** @brief One bounded touch sample handed to the gesture recognizer. */
typedef struct {
    uint32_t timestamp_ms;
    uint16_t x;
    uint16_t y;
    uint8_t pressed;
} touch_sample_t;

/** @brief Return the conservative default configuration. */
touch_input_config_t touch_input_default(void);

/** @brief Clamp one configuration into the documented bounds. */
touch_input_config_t touch_input_clamp(const touch_input_config_t *config);

/** @brief Return true when a configuration is version-compatible and bounded. */
bool touch_input_is_valid(const touch_input_config_t *config);

/** @brief Return true when a gesture is enabled by the configuration. */
bool touch_input_gesture_enabled(
    const touch_input_config_t *config,
    touch_gesture_t gesture
);

/** @brief Return the stable string for one touch error code. */
const char *touch_input_error_name(touch_input_error_t error);

/** @brief Apply the cached configuration to the touch controller. */
esp_err_t touch_input_apply(const touch_input_config_t *config);

/** @brief Initialize the touch configuration store. */
esp_err_t touch_input_init(void);

/** @brief Return true when the touch configuration store is ready. */
bool touch_input_is_ready(void);

/** @brief Store a guardian configuration, clamping out-of-range values. */
esp_err_t touch_input_set(const touch_input_config_t *config);

/** @brief Return the currently applied configuration. */
touch_input_config_t touch_input_get(void);

/**
 * @brief Classify a press/release pair into one gesture.
 *
 * The pure recognizer never reads hardware: it consumes the previous gesture
 * state and one bounded sample pair so the same logic runs in host tests and
 * on the device.
 */
touch_gesture_t touch_input_classify_release(
    const touch_input_config_t *config,
    const touch_sample_t *press,
    const touch_sample_t *release
);

/** @brief Return the removable-module descriptor for touch_input. */
const module_descriptor_t *touch_input_module_descriptor(void);

#ifdef __cplusplus
}
#endif
