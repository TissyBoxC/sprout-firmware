#pragma once

#include <stdbool.h>

#include "bluetooth_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Map a preferred codec name to the firmware enum. */
bool bluetooth_audio_sync_core_parse_codec(
    const char *value,
    bluetooth_codec_t *codec_out
);

/**
 * @brief Build one clamped policy from a fetched remote policy.
 *
 * Returns false when the preferred codec name is not in the catalog, so a
 * malformed response is rejected instead of silently defaulting the codec.
 * The resulting policy is always bounded and schema-compatible.
 */
bool bluetooth_audio_sync_core_build_policy(
    const bluetooth_audio_remote_t *remote,
    bluetooth_policy_t *policy_out
);

#ifdef __cplusplus
}
#endif
