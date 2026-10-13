#pragma once

#include <stdbool.h>

#include "image_privacy.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Map a processing-mode name to the firmware enum. */
bool image_privacy_sync_core_parse_mode(
    const char *value,
    image_processing_mode_t *mode_out
);

/** @brief Map an upload-scope name to the firmware enum. */
bool image_privacy_sync_core_parse_scope(
    const char *value,
    image_upload_scope_t *scope_out
);

/** @brief Map a redaction-level name to the firmware enum. */
bool image_privacy_sync_core_parse_redaction(
    const char *value,
    image_redaction_level_t *level_out
);

/**
 * @brief Build one clamped policy from a fetched remote policy.
 *
 * Returns false when the processing-mode, upload-scope or redaction-level name
 * is not in its catalog, so a malformed response is rejected instead of
 * silently defaulting. The resulting policy is always bounded and
 * schema-compatible.
 */
bool image_privacy_sync_core_build_policy(
    const image_privacy_remote_t *remote,
    image_privacy_policy_t *policy_out
);

#ifdef __cplusplus
}
#endif
