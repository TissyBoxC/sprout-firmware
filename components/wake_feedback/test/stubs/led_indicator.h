#pragma once

typedef enum {
    LED_INDICATOR_STATE_OFF = 0,
    LED_INDICATOR_STATE_LISTENING,
} led_indicator_state_t;

typedef enum {
    LED_INDICATOR_OK = 0,
    LED_INDICATOR_ERR_NOT_INITIALIZED,
    LED_INDICATOR_ERR_INVALID_STATE,
} led_indicator_error_t;

led_indicator_error_t led_indicator_set_state(led_indicator_state_t state);
const char *led_indicator_error_name(led_indicator_error_t error);
