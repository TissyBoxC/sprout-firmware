#pragma once

#include <stdbool.h>

#include "learning_visualization.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Map a feedback-style name to the firmware enum. */
bool learning_visualization_sync_core_parse_style(
    const char *value,
    learning_style_t *style_out
);

/** @brief Map an animation-level name to the firmware enum. */
bool learning_visualization_sync_core_parse_animation(
    const char *value,
    learning_animation_level_t *level_out
);

/**
 * @brief Build one clamped policy from a fetched remote policy.
 *
 * Returns false when the style or animation name is not in its catalog, so a
 * malformed response is rejected instead of silently defaulting. The resulting
 * policy is always bounded and schema-compatible.
 */
bool learning_visualization_sync_core_build_policy(
    const learning_visualization_remote_t *remote,
    learning_policy_t *policy_out
);

#ifdef __cplusplus
}
#endif
