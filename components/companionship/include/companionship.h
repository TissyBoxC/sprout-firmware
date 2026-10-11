#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum companion reply length including the terminator. */
#define COMPANIONSHIP_REPLY_SIZE 160

/**
 * @brief Stable emotion buckets the companion reacts to.
 *
 * The values are stable because a caller may persist or log them; they mirror
 * the platform's child emotion vocabulary without binding to any model.
 */
typedef enum {
    COMPANIONSHIP_EMOTION_NEUTRAL = 0,
    COMPANIONSHIP_EMOTION_HAPPY = 1,
    COMPANIONSHIP_EMOTION_SAD = 2,
    COMPANIONSHIP_EMOTION_ANGRY = 3,
    COMPANIONSHIP_EMOTION_SCARED = 4,
    COMPANIONSHIP_EMOTION_TIRED = 5,
} companionship_emotion_t;

/** @brief Stable companionship error codes for diagnostics and UI copy. */
typedef enum {
    COMPANIONSHIP_OK = 0,
    COMPANIONSHIP_ERR_NOT_INITIALIZED,
    COMPANIONSHIP_ERR_INVALID_ARGUMENT,
    COMPANIONSHIP_ERR_UNKNOWN_EMOTION,
} companionship_error_t;

/** @brief Initialize the companionship module. */
esp_err_t companionship_init(void);

/** @brief Return true when the companionship module is ready. */
bool companionship_is_ready(void);

/**
 * @brief Build a short, warm encouragement for one emotion.
 *
 * Writes a bounded, human-readable reply into reply_out. The device uses this
 * as the local fallback when the AI reply is unavailable, so the response
 * stays kind and never claims content the platform did not provide.
 */
companionship_error_t companionship_encourage(
    companionship_emotion_t emotion,
    const char *child_name,
    char *reply_out,
    size_t reply_size
);

/** @brief Parse a stable emotion string, or return false when unknown. */
bool companionship_emotion_from_name(
    const char *name,
    companionship_emotion_t *emotion_out
);

/** @brief Return the stable string for one emotion. */
const char *companionship_emotion_name(companionship_emotion_t emotion);

/** @brief Return the stable string for one companionship error code. */
const char *companionship_error_name(companionship_error_t error);

/** @brief Return the removable-module descriptor for companionship. */
const module_descriptor_t *companionship_module_descriptor(void);

#ifdef __cplusplus
}
#endif
