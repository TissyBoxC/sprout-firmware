#include "local_command.h"

#include <cassert>
#include <cstring>

static local_command_t make_command(
    const char *command_id,
    const char *phrase,
    local_command_action_t action,
    const char *argument,
    bool enabled
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
    command.revision = 3;
    command.enabled = enabled;
    return command;
}

int main() {
    assert(local_command_init() == ESP_OK);
    assert(local_command_is_ready());
    assert(local_command_count() == 0);

    const local_command_t commands[] = {
        make_command(
            "cmd_play",
            "听故事",
            LOCAL_COMMAND_ACTION_PLAY_PACKAGE,
            "story_001",
            true
        ),
        make_command(
            "cmd_volume",
            "小声一点",
            LOCAL_COMMAND_ACTION_SET_VOLUME,
            "30",
            true
        ),
        make_command(
            "cmd_pause",
            "停一下",
            LOCAL_COMMAND_ACTION_PAUSE,
            nullptr,
            true
        ),
    };
    assert(local_command_replace_all(commands, 3) == ESP_OK);
    assert(local_command_count() == 3);

    local_command_t listed[LOCAL_COMMAND_CAPACITY];
    size_t listed_count = 0;
    assert(
        local_command_list(listed, LOCAL_COMMAND_CAPACITY, &listed_count) ==
        ESP_OK
    );
    assert(listed_count == 3);
    assert(std::strcmp(listed[0].command_id, "cmd_play") == 0);
    assert(listed[0].revision == 3);

    local_command_t resolved;
    std::memset(&resolved, 0, sizeof(resolved));
    assert(local_command_resolve(" 听故事 ", &resolved));
    assert(resolved.action == LOCAL_COMMAND_ACTION_PLAY_PACKAGE);
    assert(std::strcmp(resolved.argument, "story_001") == 0);
    assert(local_command_resolve("小声一点", &resolved));
    assert(resolved.action == LOCAL_COMMAND_ACTION_SET_VOLUME);
    assert(!local_command_resolve("随便说说", &resolved));

    // The blob survives a re-init, standing in for a device restart.
    assert(local_command_init() == ESP_OK);
    assert(local_command_count() == 3);
    assert(local_command_resolve("停一下", &resolved));
    assert(resolved.action == LOCAL_COMMAND_ACTION_PAUSE);

    // Replacing with a smaller, updated set persists and drops the rest.
    const local_command_t replacement[] = {
        make_command(
            "cmd_sleep",
            "睡觉了",
            LOCAL_COMMAND_ACTION_SLEEP,
            nullptr,
            true
        ),
    };
    assert(local_command_replace_all(replacement, 1) == ESP_OK);
    assert(local_command_count() == 1);
    assert(local_command_init() == ESP_OK);
    assert(local_command_count() == 1);
    assert(local_command_resolve("睡觉了", &resolved));
    assert(!local_command_resolve("听故事", &resolved));

    // A malformed batch is rejected without touching the working set.
    const local_command_t malformed[] = {
        make_command(
            "cmd_bad",
            "播放",
            LOCAL_COMMAND_ACTION_PLAY_PACKAGE,
            nullptr,
            true
        ),
    };
    assert(local_command_replace_all(malformed, 1) == ESP_ERR_INVALID_ARG);
    assert(local_command_count() == 1);
    assert(local_command_resolve("睡觉了", &resolved));

    // Action name round-trips.
    assert(
        std::strcmp(
            local_command_action_name(LOCAL_COMMAND_ACTION_PLAY_PACKAGE),
            "play_package"
        ) == 0
    );
    local_command_action_t parsed;
    assert(local_command_action_from_name("set_volume", &parsed));
    assert(parsed == LOCAL_COMMAND_ACTION_SET_VOLUME);
    assert(!local_command_action_from_name("bogus", &parsed));

    // Count is reported even when the caller's buffer is smaller than the set.
    const local_command_t many[] = {
        make_command("a", "一", LOCAL_COMMAND_ACTION_STOP, nullptr, true),
        make_command("b", "二", LOCAL_COMMAND_ACTION_STOP, nullptr, true),
        make_command("c", "三", LOCAL_COMMAND_ACTION_STOP, nullptr, true),
    };
    assert(local_command_replace_all(many, 3) == ESP_OK);
    local_command_t one[1];
    size_t one_count = 0;
    assert(local_command_list(one, 1, &one_count) == ESP_OK);
    assert(one_count == 3);
    assert(std::strcmp(one[0].command_id, "a") == 0);

    return 0;
}
