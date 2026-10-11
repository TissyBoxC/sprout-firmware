#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "companionship.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Render a bounded encouragement for one emotion.
 *
 * Pure helper: no ESP-IDF dependency and no global state, so the host tests
 * can exercise every branch. `child_name` may be NULL or empty.
 */
companionship_error_t companionship_core_encourage(
    companionship_emotion_t emotion,
    const char *child_name,
    char *reply_out,
    size_t reply_size
);

#ifdef __cplusplus
}
#endif
