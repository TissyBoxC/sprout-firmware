#include "companionship_core.h"

#include <stdio.h>
#include <string.h>

static const char *emotion_reply(companionship_emotion_t emotion) {
    switch (emotion) {
        case COMPANIONSHIP_EMOTION_NEUTRAL:
            return "我在呢，想聊什么都可以。";
        case COMPANIONSHIP_EMOTION_HAPPY:
            return "看到你这么开心，我也很高兴！";
        case COMPANIONSHIP_EMOTION_SAD:
            return "难过的时候我陪着你，慢慢来。";
        case COMPANIONSHIP_EMOTION_ANGRY:
            return "生气很正常，先深呼吸，我们一起想办法。";
        case COMPANIONSHIP_EMOTION_SCARED:
            return "别怕，我就在你身边。";
        case COMPANIONSHIP_EMOTION_TIRED:
            return "累了就休息一下吧，养足精神再继续。";
        default:
            return NULL;
    }
}

companionship_error_t companionship_core_encourage(
    companionship_emotion_t emotion,
    const char *child_name,
    char *reply_out,
    size_t reply_size
) {
    if (reply_out == NULL || reply_size == 0) {
        return COMPANIONSHIP_ERR_INVALID_ARGUMENT;
    }
    const char *reply = emotion_reply(emotion);
    if (reply == NULL) {
        return COMPANIONSHIP_ERR_UNKNOWN_EMOTION;
    }
    if (child_name != NULL && child_name[0] != '\0') {
        // Prefix the child's name when the caller knows it; the bounded
        // buffer means a long name is truncated by snprintf, never overrun.
        (void)snprintf(reply_out, reply_size, "%s，%s", child_name, reply);
    } else {
        (void)snprintf(reply_out, reply_size, "%s", reply);
    }
    reply_out[reply_size - 1] = '\0';
    return COMPANIONSHIP_OK;
}
