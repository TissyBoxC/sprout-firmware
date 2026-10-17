#pragma once

#include <stddef.h>

#include "local_command_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Replace the local command store with one remote snapshot.
 *
 * Remote commands are applied in order, dropping duplicate command ids and any
 * entry whose id, phrase, action, or argument is unusable. A snapshot that
 * cannot fit the local capacity in full is rejected rather than silently
 * truncated. An empty remote snapshot clears the store because the guardian
 * may deliberately remove every macro. A malformed snapshot leaves the store
 * unchanged, so a transient provider error cannot wipe the working offline
 * phrases.
 */
local_command_error_t local_command_sync_core_replace(
    local_command_core_t *core,
    const local_command_t *commands,
    size_t command_count
);

#ifdef __cplusplus
}
#endif
