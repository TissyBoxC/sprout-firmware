#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "local_command.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure local command store kept in publish order. */
typedef struct {
    local_command_t commands[LOCAL_COMMAND_CAPACITY];
    size_t count;
} local_command_core_t;

/** @brief Reset the store to empty. */
void local_command_core_init(local_command_core_t *core);

/** @brief Validate and replace the whole store. */
local_command_error_t local_command_core_replace_all(
    local_command_core_t *core,
    const local_command_t *commands,
    size_t command_count
);

/** @brief Resolve a trigger phrase to the matching enabled command. */
bool local_command_core_resolve(
    const local_command_core_t *core,
    const char *phrase,
    local_command_t *command_out
);

#ifdef __cplusplus
}
#endif
