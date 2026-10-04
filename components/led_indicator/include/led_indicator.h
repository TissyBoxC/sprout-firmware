#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Device states represented by the status LED. */
typedef enum {
    LED_INDICATOR_STATE_OFF = 0,
    LED_INDICATOR_STATE_BOOTING,
    LED_INDICATOR_STATE_PROVISIONING,
    LED_INDICATOR_STATE_WIFI_CONNECTING,
    LED_INDICATOR_STATE_CONNECTING,
    LED_INDICATOR_STATE_IDLE,
    LED_INDICATOR_STATE_LISTENING,
    LED_INDICATOR_STATE_THINKING,
    LED_INDICATOR_STATE_SPEAKING,
    LED_INDICATOR_STATE_MUTED,
    LED_INDICATOR_STATE_ERROR,
    LED_INDICATOR_STATE_LOW_BATTERY,
    LED_INDICATOR_STATE_FACTORY_RESET,
    LED_INDICATOR_STATE_OTA_UPGRADING,
    LED_INDICATOR_STATE_COUNT,
} led_indicator_state_t;

/** @brief Stable errors returned by the LED indicator. */
typedef enum {
    LED_INDICATOR_OK = 0,
    LED_INDICATOR_ERR_NOT_INITIALIZED,
    LED_INDICATOR_ERR_INVALID_STATE,
    LED_INDICATOR_ERR_GPIO_UNAVAILABLE,
    LED_INDICATOR_ERR_NO_MEM,
} led_indicator_error_t;

/** @brief Bounded indicator state for diagnostics and device UI. */
typedef struct {
    led_indicator_state_t state;
    bool is_enabled;
    bool is_ready;
    uint32_t transitions;
    led_indicator_error_t last_error;
} led_indicator_snapshot_t;

/**
 * @brief Initialize the status LED and its pattern task.
 *
 * The task and PWM channel stay owned by this module. If the configured GPIO
 * or LEDC peripheral is unavailable, the module returns an error and remains
 * not ready without driving the pin.
 */
esp_err_t led_indicator_init(void);

/** @brief Return true when the indicator can drive its LED. */
bool led_indicator_is_ready(void);

/**
 * @brief Select the state represented by the LED.
 *
 * The state changes the active pattern on the indicator task. Repeated calls
 * with the same state are harmless, and an invalid state is rejected.
 */
led_indicator_error_t led_indicator_set_state(
    led_indicator_state_t state
);

/** @brief Return the selected indicator state. */
led_indicator_state_t led_indicator_get_state(void);

/**
 * @brief Enable or disable the physical indicator.
 *
 * Disabling forces the LED off and stops pattern playback. Re-enabling
 * restores the currently selected state and its pattern.
 */
esp_err_t led_indicator_set_enabled(bool is_enabled);

/** @brief Return the bounded indicator snapshot. */
led_indicator_snapshot_t led_indicator_get_snapshot(void);

/** @brief Return the stable string for one indicator state. */
const char *led_indicator_state_name(led_indicator_state_t state);

/** @brief Return the stable string for one indicator error. */
const char *led_indicator_error_name(led_indicator_error_t error);

/** @brief Return the removable-module descriptor for led_indicator. */
const module_descriptor_t *led_indicator_module_descriptor(void);

#ifdef __cplusplus
}
#endif
