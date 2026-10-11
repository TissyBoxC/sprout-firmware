#include "companionship.h"

#include <string.h>

#include "companionship_core.h"

static bool s_ready;

esp_err_t companionship_init(void) {
    s_ready = true;
    return ESP_OK;
}

bool companionship_is_ready(void) {
    return s_ready;
}

companionship_error_t companionship_encourage(
    companionship_emotion_t emotion,
    const char *child_name,
    char *reply_out,
    size_t reply_size
) {
    if (!s_ready) {
        return COMPANIONSHIP_ERR_NOT_INITIALIZED;
    }
    return companionship_core_encourage(
        emotion,
        child_name,
        reply_out,
        reply_size
    );
}

bool companionship_emotion_from_name(
    const char *name,
    companionship_emotion_t *emotion_out
) {
    if (name == NULL) {
        return false;
    }
    companionship_emotion_t parsed;
    if (strcmp(name, "neutral") == 0) {
        parsed = COMPANIONSHIP_EMOTION_NEUTRAL;
    } else if (strcmp(name, "happy") == 0) {
        parsed = COMPANIONSHIP_EMOTION_HAPPY;
    } else if (strcmp(name, "sad") == 0) {
        parsed = COMPANIONSHIP_EMOTION_SAD;
    } else if (strcmp(name, "angry") == 0) {
        parsed = COMPANIONSHIP_EMOTION_ANGRY;
    } else if (strcmp(name, "scared") == 0) {
        parsed = COMPANIONSHIP_EMOTION_SCARED;
    } else if (strcmp(name, "tired") == 0) {
        parsed = COMPANIONSHIP_EMOTION_TIRED;
    } else {
        return false;
    }
    if (emotion_out != NULL) {
        *emotion_out = parsed;
    }
    return true;
}

const char *companionship_emotion_name(companionship_emotion_t emotion) {
    switch (emotion) {
        case COMPANIONSHIP_EMOTION_NEUTRAL:
            return "neutral";
        case COMPANIONSHIP_EMOTION_HAPPY:
            return "happy";
        case COMPANIONSHIP_EMOTION_SAD:
            return "sad";
        case COMPANIONSHIP_EMOTION_ANGRY:
            return "angry";
        case COMPANIONSHIP_EMOTION_SCARED:
            return "scared";
        case COMPANIONSHIP_EMOTION_TIRED:
            return "tired";
        default:
            return "unknown";
    }
}

const char *companionship_error_name(companionship_error_t error) {
    switch (error) {
        case COMPANIONSHIP_OK:
            return "ok";
        case COMPANIONSHIP_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case COMPANIONSHIP_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case COMPANIONSHIP_ERR_UNKNOWN_EMOTION:
            return "unknown_emotion";
        default:
            return "unknown";
    }
}

static esp_err_t companionship_initialize(void) {
    return companionship_init();
}

const module_descriptor_t *companionship_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "companionship",
        .version = "1.0.0",
        .initialize = companionship_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
