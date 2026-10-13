#pragma once

#include <stdbool.h>

#include "video_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Map a quality-floor name to the firmware enum. */
bool video_session_sync_core_parse_quality(
    const char *value,
    video_quality_t *quality_out
);

/**
 * @brief Build one clamped policy from a fetched remote policy.
 *
 * Returns false when the quality-floor name is not in its catalog, so a
 * malformed response is rejected instead of silently defaulting. The resulting
 * policy is always bounded and schema-compatible.
 */
bool video_session_sync_core_build_policy(
    const video_remote_t *remote,
    video_policy_t *policy_out
);

#ifdef __cplusplus
}
#endif
