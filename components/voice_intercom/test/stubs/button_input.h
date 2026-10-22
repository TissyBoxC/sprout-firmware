#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_INPUT_MAX_BUTTONS 2

typedef enum {
    BUTTON_INPUT_GESTURE_NONE = 0,
    BUTTON_INPUT_GESTURE_SHORT_PRESS,
    BUTTON_INPUT_GESTURE_LONG_PRESS,
    BUTTON_INPUT_GESTURE_VERY_LONG_PRESS,
    BUTTON_INPUT_GESTURE_DOUBLE_PRESS,
} button_input_gesture_t;

#ifdef __cplusplus
}
#endif
