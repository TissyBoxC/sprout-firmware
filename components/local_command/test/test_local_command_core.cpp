#include "local_command_core.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static local_command_t make_command(
    const char *command_id,
    const char *phrase,
    local_command_action_t action,
    const char *argument
) {
    local_command_t command;
    std::memset(&command, 0, sizeof(command));
    std::strncpy(
        command.command_id,
        command_id,
        LOCAL_COMMAND_ID_SIZE - 1
    );
    std::strncpy(command.phrase, phrase, LOCAL_COMMAND_PHRASE_SIZE - 1);
    command.action = action;
    if (argument != nullptr) {
        std::strncpy(
            command.argument,
            argument,
            LOCAL_COMMAND_ARGUMENT_SIZE - 1
        );
    }
    command.revision = 1;
    command.enabled = true;
    return command;
}

int main() {
    local_command_core_t core;
    local_command_core_init(&core);
    assert(core.count == 0);

    const local_command_t first = make_command(
        "cmd_play",
        "听故事",
        LOCAL_COMMAND_ACTION_PLAY_PACKAGE,
        "story_001"
    );
    const local_command_t second = make_command(
        "cmd_pause",
        "停一下",
        LOCAL_COMMAND_ACTION_PAUSE,
        nullptr
    );
    const local_command_t batch[] = {first, second};
    assert(
        local_command_core_replace_all(&core, batch, 2) == LOCAL_COMMAND_OK
    );
    assert(core.count == 2);
    assert(std::strcmp(core.commands[0].command_id, "cmd_play") == 0);
    assert(std::strcmp(core.commands[1].command_id, "cmd_pause") == 0);

    // Resolution trims whitespace and folds to lower case.
    local_command_t resolved;
    std::memset(&resolved, 0, sizeof(resolved));
    assert(local_command_core_resolve(&core, "  听故事 ", &resolved));
    assert(resolved.action == LOCAL_COMMAND_ACTION_PLAY_PACKAGE);
    assert(std::strcmp(resolved.argument, "story_001") == 0);
    assert(local_command_core_resolve(&core, "停一下", &resolved));
    assert(resolved.action == LOCAL_COMMAND_ACTION_PAUSE);
    assert(!local_command_core_resolve(&core, "不存在的口令", &resolved));
    assert(!local_command_core_resolve(&core, "   ", &resolved));

    // A disabled command never resolves.
    local_command_t disabled = make_command(
        "cmd_sleep",
        "睡觉了",
        LOCAL_COMMAND_ACTION_SLEEP,
        nullptr
    );
    disabled.enabled = false;
    assert(
        local_command_core_replace_all(&core, &disabled, 1) ==
        LOCAL_COMMAND_OK
    );
    assert(!local_command_core_resolve(&core, "睡觉了", &resolved));

    // A target-bearing action without an argument is rejected, so the whole
    // batch is refused and the working set is untouched.
    local_command_t missing_argument = make_command(
        "cmd_bad",
        "播放",
        LOCAL_COMMAND_ACTION_PLAY_PACKAGE,
        nullptr
    );
    const local_command_t good[] = {first};
    assert(
        local_command_core_replace_all(&core, good, 1) == LOCAL_COMMAND_OK
    );
    assert(
        local_command_core_replace_all(&core, &missing_argument, 1) ==
        LOCAL_COMMAND_ERR_INVALID_ARGUMENT
    );
    assert(core.count == 1);
    assert(std::strcmp(core.commands[0].command_id, "cmd_play") == 0);

    // A stateless action carrying an argument is also rejected.
    local_command_t stray_argument = make_command(
        "cmd_stray",
        "暂停",
        LOCAL_COMMAND_ACTION_PAUSE,
        "unexpected"
    );
    assert(
        local_command_core_replace_all(&core, &stray_argument, 1) ==
        LOCAL_COMMAND_ERR_INVALID_ARGUMENT
    );

    // An unknown action is rejected.
    local_command_t unknown_action = make_command(
        "cmd_unknown",
        "未知",
        LOCAL_COMMAND_ACTION_NONE,
        nullptr
    );
    assert(
        local_command_core_replace_all(&core, &unknown_action, 1) ==
        LOCAL_COMMAND_ERR_INVALID_ARGUMENT
    );

    // Capacity is bounded.
    local_command_t oversized[LOCAL_COMMAND_CAPACITY + 1];
    char id[LOCAL_COMMAND_ID_SIZE];
    for (int index = 0; index < LOCAL_COMMAND_CAPACITY + 1; ++index) {
        std::snprintf(id, sizeof(id), "cmd_%03d", index);
        oversized[index] = make_command(
            id,
            "口令",
            LOCAL_COMMAND_ACTION_STOP,
            nullptr
        );
    }
    assert(
        local_command_core_replace_all(
            &core,
            oversized,
            LOCAL_COMMAND_CAPACITY + 1
        ) == LOCAL_COMMAND_ERR_FULL
    );

    // A null store reports NOT_INITIALIZED.
    assert(
        local_command_core_replace_all(nullptr, batch, 2) ==
        LOCAL_COMMAND_ERR_NOT_INITIALIZED
    );

    return 0;
}
