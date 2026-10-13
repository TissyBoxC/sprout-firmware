#include "local_command_sync_core.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static local_command_t make_command(
    const char *command_id,
    const char *phrase,
    local_command_action_t action,
    const char *argument
) {
    local_command_t command = {};
    std::strncpy(command.command_id, command_id, LOCAL_COMMAND_ID_SIZE - 1);
    std::strncpy(command.phrase, phrase, LOCAL_COMMAND_PHRASE_SIZE - 1);
    command.action = action;
    if (argument != nullptr) {
        std::strncpy(
            command.argument,
            argument,
            LOCAL_COMMAND_ARGUMENT_SIZE - 1
        );
    }
    command.revision = 4;
    command.enabled = true;
    return command;
}

int main() {
    local_command_core_t core = {};
    local_command_core_init(&core);
    const local_command_t local = make_command(
        "cmd_local",
        "本地口令",
        LOCAL_COMMAND_ACTION_PAUSE,
        nullptr
    );
    assert(local_command_core_replace_all(&core, &local, 1) == LOCAL_COMMAND_OK);

    local_command_t remote[] = {
        make_command(
            "cmd_play",
            "播放睡前故事",
            LOCAL_COMMAND_ACTION_PLAY_PACKAGE,
            "content_story_001"
        ),
        make_command(
            "cmd_stop",
            "停止播放",
            LOCAL_COMMAND_ACTION_STOP,
            nullptr
        ),
        // Duplicate id is dropped.
        make_command(
            "cmd_play",
            "重复口令",
            LOCAL_COMMAND_ACTION_PAUSE,
            nullptr
        ),
        // Invalid: play_package without an argument.
        make_command(
            "cmd_bad",
            "无目标播放",
            LOCAL_COMMAND_ACTION_PLAY_PACKAGE,
            nullptr
        ),
    };
    assert(local_command_sync_core_replace(&core, remote, 4) == LOCAL_COMMAND_OK);
    assert(core.count == 2);
    assert(std::strcmp(core.commands[0].command_id, "cmd_play") == 0);
    assert(std::strcmp(core.commands[1].command_id, "cmd_stop") == 0);
    assert(
        std::strcmp(
            core.commands[0].argument,
            "content_story_001"
        ) == 0
    );

    // An empty valid remote snapshot is authoritative and clears the store.
    assert(local_command_sync_core_replace(&core, nullptr, 0) == LOCAL_COMMAND_OK);
    assert(core.count == 0);

    // A snapshot that only contains invalid entries is rejected.
    const local_command_t valid = make_command(
        "cmd_stop",
        "停止播放",
        LOCAL_COMMAND_ACTION_STOP,
        nullptr
    );
    assert(local_command_core_replace_all(&core, &valid, 1) == LOCAL_COMMAND_OK);
    local_command_t invalid_only[] = {
        make_command("", "无编号", LOCAL_COMMAND_ACTION_STOP, nullptr),
        make_command(
            "cmd_bad",
            "参数不匹配",
            LOCAL_COMMAND_ACTION_STOP,
            "unexpected"
        ),
    };
    assert(
        local_command_sync_core_replace(&core, invalid_only, 2) ==
        LOCAL_COMMAND_ERR_INVALID_ARGUMENT
    );
    assert(core.count == 1);
    assert(std::strcmp(core.commands[0].command_id, "cmd_stop") == 0);

    // Capacity is bounded without rejecting the whole snapshot.
    local_command_t too_many[LOCAL_COMMAND_CAPACITY + 2] = {};
    char ids[LOCAL_COMMAND_CAPACITY + 2][LOCAL_COMMAND_ID_SIZE] = {};
    char phrases[LOCAL_COMMAND_CAPACITY + 2][LOCAL_COMMAND_PHRASE_SIZE] = {};
    for (int index = 0; index < LOCAL_COMMAND_CAPACITY + 2; ++index) {
        std::snprintf(ids[index], sizeof(ids[index]), "cmd_%03d", index);
        std::snprintf(
            phrases[index],
            sizeof(phrases[index]),
            "口令 %03d",
            index
        );
        too_many[index] = make_command(
            ids[index],
            phrases[index],
            LOCAL_COMMAND_ACTION_STOP,
            nullptr
        );
    }
    assert(
        local_command_sync_core_replace(
            &core,
            too_many,
            LOCAL_COMMAND_CAPACITY + 2
        ) == LOCAL_COMMAND_OK
    );
    assert(core.count == LOCAL_COMMAND_CAPACITY);
    assert(std::strcmp(core.commands[0].command_id, "cmd_000") == 0);

    return 0;
}
