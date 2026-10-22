#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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

typedef enum {
    PROMPT_TONE_OK = 0,
    PROMPT_TONE_ERR_NOT_INITIALIZED,
    PROMPT_TONE_ERR_INVALID_TONE,
    PROMPT_TONE_ERR_QUEUE,
} prompt_tone_error_t;

prompt_tone_error_t prompt_tone_play(prompt_tone_t tone);

#ifdef __cplusplus
}
#endif
