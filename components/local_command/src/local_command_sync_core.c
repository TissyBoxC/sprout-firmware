#include "local_command_sync_core.h"

#include <string.h>

// Reuse the strict validators from the pure store by staging each candidate
// through a one-entry replace, so the remote path and the guardian path agree
// on exactly what a usable command is.
static bool local_command_sync_is_valid(const local_command_t *command) {
    if (command == NULL) {
        return false;
    }
    local_command_core_t probe;
    return local_command_core_replace_all(&probe, command, 1) ==
        LOCAL_COMMAND_OK;
}

static bool local_command_sync_contains(
    const char seen_ids[][LOCAL_COMMAND_ID_SIZE],
    size_t seen_count,
    const char *command_id
) {
    for (size_t index = 0; index < seen_count; ++index) {
        if (strcmp(seen_ids[index], command_id) == 0) {
            return true;
        }
    }
    return false;
}

local_command_error_t local_command_sync_core_replace(
    local_command_core_t *core,
    const local_command_t *commands,
    size_t command_count
) {
    if (core == NULL) {
        return LOCAL_COMMAND_ERR_NOT_INITIALIZED;
    }
    if (commands == NULL && command_count > 0) {
        return LOCAL_COMMAND_ERR_INVALID_ARGUMENT;
    }
    if (command_count == 0) {
        local_command_core_init(core);
        return LOCAL_COMMAND_OK;
    }

    // Track which entries were accepted so the second pass copies them into
    // the store without a second full-size store on the stack.
    char seen_ids[LOCAL_COMMAND_CAPACITY][LOCAL_COMMAND_ID_SIZE] = {};
    size_t chosen[LOCAL_COMMAND_CAPACITY] = {};
    size_t chosen_count = 0;
    for (size_t index = 0; index < command_count; ++index) {
        const local_command_t *command = &commands[index];
        if (!local_command_sync_is_valid(command) ||
            local_command_sync_contains(
                seen_ids,
                chosen_count,
                command->command_id
            )) {
            continue;
        }
        if (chosen_count >= LOCAL_COMMAND_CAPACITY) {
            break;
        }
        memcpy(
            seen_ids[chosen_count],
            command->command_id,
            LOCAL_COMMAND_ID_SIZE
        );
        chosen[chosen_count] = index;
        chosen_count += 1;
    }
    if (chosen_count == 0) {
        return LOCAL_COMMAND_ERR_INVALID_ARGUMENT;
    }
    memset(core->commands, 0, sizeof(core->commands));
    for (size_t index = 0; index < chosen_count; ++index) {
        core->commands[index] = commands[chosen[index]];
    }
    core->count = chosen_count;
    return LOCAL_COMMAND_OK;
}
