#include "companionship_core.h"

#include <cassert>
#include <cstring>

int main() {
    char reply[COMPANIONSHIP_REPLY_SIZE] = {};

    // Every stable emotion renders a non-empty reply.
    const companionship_emotion_t emotions[] = {
        COMPANIONSHIP_EMOTION_NEUTRAL,
        COMPANIONSHIP_EMOTION_HAPPY,
        COMPANIONSHIP_EMOTION_SAD,
        COMPANIONSHIP_EMOTION_ANGRY,
        COMPANIONSHIP_EMOTION_SCARED,
        COMPANIONSHIP_EMOTION_TIRED,
    };
    for (companionship_emotion_t emotion : emotions) {
        std::memset(reply, 0, sizeof(reply));
        assert(
            companionship_core_encourage(emotion, nullptr, reply, sizeof(reply)) ==
            COMPANIONSHIP_OK
        );
        assert(reply[0] != '\0');
    }

    // A known child name is prefixed.
    std::memset(reply, 0, sizeof(reply));
    assert(
        companionship_core_encourage(
            COMPANIONSHIP_EMOTION_HAPPY,
            "豆豆",
            reply,
            sizeof(reply)
        ) == COMPANIONSHIP_OK
    );
    assert(std::strncmp(reply, "豆豆，", std::strlen("豆豆，")) == 0);

    // An empty name is treated the same as no name.
    char named[COMPANIONSHIP_REPLY_SIZE] = {};
    char unnamed[COMPANIONSHIP_REPLY_SIZE] = {};
    assert(
        companionship_core_encourage(
            COMPANIONSHIP_EMOTION_SAD,
            "豆豆",
            named,
            sizeof(named)
        ) == COMPANIONSHIP_OK
    );
    assert(
        companionship_core_encourage(
            COMPANIONSHIP_EMOTION_SAD,
            "",
            unnamed,
            sizeof(unnamed)
        ) == COMPANIONSHIP_OK
    );
    assert(std::strcmp(named, unnamed) != 0);
    assert(unnamed[0] != '\0');

    // Invalid arguments are rejected.
    assert(
        companionship_core_encourage(
            COMPANIONSHIP_EMOTION_HAPPY,
            nullptr,
            nullptr,
            sizeof(reply)
        ) == COMPANIONSHIP_ERR_INVALID_ARGUMENT
    );
    assert(
        companionship_core_encourage(
            COMPANIONSHIP_EMOTION_HAPPY,
            nullptr,
            reply,
            0
        ) == COMPANIONSHIP_ERR_INVALID_ARGUMENT
    );

    // An unknown emotion reports a distinct error.
    assert(
        companionship_core_encourage(
            static_cast<companionship_emotion_t>(99),
            nullptr,
            reply,
            sizeof(reply)
        ) == COMPANIONSHIP_ERR_UNKNOWN_EMOTION
    );

    // A tiny buffer still NUL-terminates without overrunning.
    char tiny[8] = {};
    assert(
        companionship_core_encourage(
            COMPANIONSHIP_EMOTION_HAPPY,
            "非常非常长的名字",
            tiny,
            sizeof(tiny)
        ) == COMPANIONSHIP_OK
    );
    assert(tiny[sizeof(tiny) - 1] == '\0');

    return 0;
}
