#pragma once

#include <stddef.h>

#include "playback_request_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Merge one remote pending-request snapshot into the local cache.
 *
 * The device endpoint only returns requests that still need action (pending or
 * acknowledged), so this is a merge rather than a replace:
 *
 * - A remote request the device has not already finished is inserted.
 * - A remote request the device already finished (a terminal local entry) is
 *   ignored so the server's pending view cannot resurrect a played request.
 * - A local non-terminal request that is absent from the remote snapshot was
 *   cancelled or removed by the guardian, so it is dropped.
 * - Local terminal history is retained so the device keeps its recent outcomes.
 *
 * Unusable or duplicate remote ids are dropped and the result is bounded to the
 * local capacity.
 */
playback_request_error_t playback_request_sync_core_merge(
    playback_request_core_t *core,
    const playback_request_t *remote,
    size_t remote_count
);

#ifdef __cplusplus
}
#endif
