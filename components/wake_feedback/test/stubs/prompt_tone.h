#pragma once

typedef enum {
    PROMPT_TONE_WAKE_ACCEPTED = 0,
    PROMPT_TONE_CAPTURE_STARTED,
} prompt_tone_t;

typedef enum {
    PROMPT_TONE_OK = 0,
    PROMPT_TONE_ERR_NOT_INITIALIZED,
    PROMPT_TONE_ERR_INVALID_TONE,
    PROMPT_TONE_ERR_QUEUE,
} prompt_tone_error_t;

prompt_tone_error_t prompt_tone_play(prompt_tone_t tone);
const char *prompt_tone_error_name(prompt_tone_error_t error);
