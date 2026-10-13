#pragma once

#include <stdint.h>

#include "offline_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Clamp a signed byte count into the approved cache-budget bounds. */
int64_t offline_policy_clamp_cache_bytes(int64_t value);

/** @brief Return true when a remote revision may replace the applied one. */
bool offline_policy_core_accepts_revision(
    uint32_t current_revision,
    uint32_t remote_revision
);

#ifdef __cplusplus
}
#endif
