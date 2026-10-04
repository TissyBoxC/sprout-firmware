#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of configured physical buttons. */
#define BUTTON_INPUT_MAX_BUTTONS 2

/** @brief Stable gestures reported by the button state machine. */
typedef enum {
    BUTTON_INPUT_GESTURE_NONE = 0,
    BUTTON_INPUT_GESTURE_SHORT_PRESS,
    BUTTON_INPUT_GESTURE_LONG_PRESS,
    BUTTON_INPUT_GESTURE_VERY_LONG_PRESS,
    BUTTON_INPUT_GESTURE_DOUBLE_PRESS,
} button_input_gesture_t;

/** @brief Stable errors returned by the button module. */
typedef enum {
    BUTTON_INPUT_OK = 0,
    BUTTON_INPUT_ERR_NOT_INITIALIZED,
    BUTTON_INPUT_ERR_INVALID_ARGUMENT,
    BUTTON_INPUT_ERR_GPIO,
    BUTTON_INPUT_ERR_NO_MEM,
} button_input_error_t;

/** @brief Bounded event delivered to the registered handler. */
typedef struct {
    uint8_t button_index;
    button_input_gesture_t gesture;
    uint32_t held_ms;
    uint32_t timestamp_ms;
} button_input_event_t;

/** @brief Bounded per-button state and gesture counters. */
typedef struct {
    uint8_t button_count;
    bool is_pressed[BUTTON_INPUT_MAX_BUTTONS];
    uint32_t short_press_count[BUTTON_INPUT_MAX_BUTTONS];
    uint32_t long_press_count[BUTTON_INPUT_MAX_BUTTONS];
    uint32_t double_press_count[BUTTON_INPUT_MAX_BUTTONS];
    uint32_t very_long_press_count[BUTTON_INPUT_MAX_BUTTONS];
} button_input_snapshot_t;

/** @brief Callback invoked for one classified gesture. */
typedef void (*button_input_event_handler_t)(const button_input_event_t *event);

/**
 * @brief Initialize the configured buttons and start the debounce task.
 *
 * Configures the primary and optional secondary GPIOs as inputs, installs the
 * shared GPIO ISR service when needed, and creates the bounded button task.
 * Any GPIO failure leaves the module not-ready and releases partial resources.
 */
esp_err_t button_input_init(void);

/** @brief Return true when the button task is running and delivering events. */
bool button_input_is_ready(void);

/**
 * @brief Set the single event handler.
 *
 * Passing NULL clears the handler. The handler runs in the normal button task
 * context and may call non-ISR APIs, but it must return promptly so later
 * gestures are not delayed.
 */
esp_err_t button_input_set_event_handler(button_input_event_handler_t handler);

/** @brief Return a consistent per-button counter snapshot. */
button_input_snapshot_t button_input_get_snapshot(void);

/** @brief Return the stable string for one gesture. */
const char *button_input_gesture_name(button_input_gesture_t gesture);

/** @brief Return the stable string for one button error code. */
const char *button_input_error_name(button_input_error_t error);

/** @brief Return the removable-module descriptor for button_input. */
const module_descriptor_t *button_input_module_descriptor(void);

#ifdef __cplusplus
}
#endif
