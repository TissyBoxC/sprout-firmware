#include "local_command_core.h"

#include <ctype.h>
#include <string.h>

static bool local_command_action_is_valid(local_command_action_t action) {
    switch (action) {
        case LOCAL_COMMAND_ACTION_PLAY_PACKAGE:
        case LOCAL_COMMAND_ACTION_PAUSE:
        case LOCAL_COMMAND_ACTION_RESUME:
        case LOCAL_COMMAND_ACTION_STOP:
        case LOCAL_COMMAND_ACTION_SET_VOLUME:
        case LOCAL_COMMAND_ACTION_SLEEP:
            return true;
        default:
            return false;
    }
}

static bool local_command_is_valid(const local_command_t *command) {
    if (command == NULL) {
        return false;
    }
    if (command->command_id[0] == '\0' || command->phrase[0] == '\0') {
        return false;
    }
    if (!local_command_action_is_valid(command->action)) {
        return false;
    }
    // An action that needs a target must carry one; a stateless action must
    // not, so a malformed macro cannot silently run against the wrong target.
    switch (command->action) {
        case LOCAL_COMMAND_ACTION_PLAY_PACKAGE:
            return command->argument[0] != '\0';
        case LOCAL_COMMAND_ACTION_SET_VOLUME:
            return command->argument[0] != '\0';
        case LOCAL_COMMAND_ACTION_PAUSE:
        case LOCAL_COMMAND_ACTION_RESUME:
        case LOCAL_COMMAND_ACTION_STOP:
        case LOCAL_COMMAND_ACTION_SLEEP:
            return command->argument[0] == '\0';
        default:
            return false;
    }
}

void local_command_core_init(local_command_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

local_command_error_t local_command_core_replace_all(
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
    if (command_count > LOCAL_COMMAND_CAPACITY) {
        return LOCAL_COMMAND_ERR_FULL;
    }
    for (size_t index = 0; index < command_count; ++index) {
        if (!local_command_is_valid(&commands[index])) {
            return LOCAL_COMMAND_ERR_INVALID_ARGUMENT;
        }
    }
    memset(core, 0, sizeof(*core));
    for (size_t index = 0; index < command_count; ++index) {
        core->commands[index] = commands[index];
    }
    core->count = command_count;
    return LOCAL_COMMAND_OK;
}

static void local_command_normalize(const char *input, char *output, size_t size) {
    if (output == NULL || size == 0) {
        return;
    }
    output[0] = '\0';
    if (input == NULL) {
        return;
    }
    size_t written = 0;
    for (const char *cursor = input; *cursor != '\0' && written + 1 < size;
         ++cursor) {
        const unsigned char current = (unsigned char)*cursor;
        if (isspace(current)) {
            continue;
        }
        output[written] = (char)tolower(current);
        written += 1;
    }
    output[written] = '\0';
}

bool local_command_core_resolve(
    const local_command_core_t *core,
    const char *phrase,
    local_command_t *command_out
) {
    if (core == NULL || phrase == NULL) {
        return false;
    }
    char normalized[LOCAL_COMMAND_PHRASE_SIZE];
    local_command_normalize(phrase, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        return false;
    }
    for (size_t index = 0; index < core->count; ++index) {
        const local_command_t *command = &core->commands[index];
        if (!command->enabled) {
            continue;
        }
        char candidate[LOCAL_COMMAND_PHRASE_SIZE];
        local_command_normalize(
            command->phrase,
            candidate,
            sizeof(candidate)
        );
        if (strcmp(candidate, normalized) != 0) {
            continue;
        }
        if (command_out != NULL) {
            *command_out = *command;
        }
        return true;
    }
    return false;
}
