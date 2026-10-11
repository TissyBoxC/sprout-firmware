#include "form_factor_profile.h"

#include <stddef.h>
#include <string.h>

uint32_t form_factor_compose_actions(uint32_t capability_mask) {
    const uint32_t caps = capability_mask & FORM_FACTOR_CAP_ALL;
    uint32_t actions = 0u;
    if ((caps & FORM_FACTOR_CAP_DISPLAY) != 0u) {
        actions |= FORM_FACTOR_ACTION_EXPRESSION;
        actions |= FORM_FACTOR_ACTION_VIDEO_DISPLAY;
    }
    if ((caps & FORM_FACTOR_CAP_TOUCH) != 0u) {
        actions |= FORM_FACTOR_ACTION_TOUCH;
    }
    if ((caps & FORM_FACTOR_CAP_LOCOMOTION) != 0u) {
        actions |= FORM_FACTOR_ACTION_WALK;
        actions |= FORM_FACTOR_ACTION_DANCE;
        actions |= FORM_FACTOR_ACTION_GESTURE;
    }
    if ((caps & FORM_FACTOR_CAP_SERVO) != 0u) {
        actions |= FORM_FACTOR_ACTION_TAIL;
        actions |= FORM_FACTOR_ACTION_GESTURE;
    }
    const uint32_t voice_inputs =
        FORM_FACTOR_CAP_MICROPHONE | FORM_FACTOR_CAP_SPEAKER;
    if ((caps & voice_inputs) == voice_inputs) {
        actions |= FORM_FACTOR_ACTION_VOICE;
    }
    if ((caps & FORM_FACTOR_CAP_CAMERA) != 0u &&
        (caps & FORM_FACTOR_CAP_DISPLAY) != 0u) {
        actions |= FORM_FACTOR_ACTION_VIDEO_CALL;
    }
    if ((caps & FORM_FACTOR_CAP_WEARABLE) != 0u) {
        actions |= FORM_FACTOR_ACTION_LOCATION;
    }
    return actions;
}

form_factor_profile_t form_factor_default(void) {
    form_factor_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.schema_version = FORM_FACTOR_SCHEMA_VERSION;
    // The default is a stationary desktop body: screen, speaker, microphone,
    // touch and a battery, with no locomotion or servo.
    profile.capability_mask =
        FORM_FACTOR_CAP_DISPLAY |
        FORM_FACTOR_CAP_MICROPHONE |
        FORM_FACTOR_CAP_SPEAKER |
        FORM_FACTOR_CAP_TOUCH |
        FORM_FACTOR_CAP_BATTERY;
    const char *name = "desktop";
    memcpy(profile.technical_name, name, strlen(name));
    profile.action_mask = form_factor_compose_actions(profile.capability_mask);
    return profile;
}

form_factor_profile_t form_factor_clamp(const form_factor_profile_t *profile) {
    form_factor_profile_t clamped = form_factor_default();
    if (profile == NULL) {
        return clamped;
    }
    clamped.schema_version = FORM_FACTOR_SCHEMA_VERSION;
    memset(clamped.technical_name, 0, sizeof(clamped.technical_name));
    const size_t length = strnlen(
        profile->technical_name,
        FORM_FACTOR_NAME_MAX - 1u
    );
    memcpy(clamped.technical_name, profile->technical_name, length);
    clamped.capability_mask =
        profile->capability_mask & FORM_FACTOR_CAP_ALL;
    // The action set always follows the capability set so a swapped body can
    // never advertise an action its hardware cannot perform.
    clamped.action_mask = form_factor_compose_actions(clamped.capability_mask);
    return clamped;
}

bool form_factor_is_valid(const form_factor_profile_t *profile) {
    if (profile == NULL) {
        return false;
    }
    if (profile->schema_version != FORM_FACTOR_SCHEMA_VERSION) {
        return false;
    }
    if ((profile->capability_mask & ~FORM_FACTOR_CAP_ALL) != 0u) {
        return false;
    }
    if ((profile->action_mask & ~FORM_FACTOR_ACTION_ALL) != 0u) {
        return false;
    }
    // The profile must be self-consistent: the actions must match the
    // capabilities, and the name must be a non-empty NUL-terminated string.
    if (profile->action_mask !=
        form_factor_compose_actions(profile->capability_mask)) {
        return false;
    }
    if (profile->technical_name[FORM_FACTOR_NAME_MAX - 1u] != '\0') {
        return false;
    }
    return profile->technical_name[0] != '\0';
}

const char *form_factor_capability_name(uint32_t capability) {
    switch (capability) {
        case FORM_FACTOR_CAP_DISPLAY:
            return "display";
        case FORM_FACTOR_CAP_CAMERA:
            return "camera";
        case FORM_FACTOR_CAP_MICROPHONE:
            return "microphone";
        case FORM_FACTOR_CAP_SPEAKER:
            return "speaker";
        case FORM_FACTOR_CAP_TOUCH:
            return "touch";
        case FORM_FACTOR_CAP_LOCOMOTION:
            return "locomotion";
        case FORM_FACTOR_CAP_SERVO:
            return "servo";
        case FORM_FACTOR_CAP_CELLULAR:
            return "cellular";
        case FORM_FACTOR_CAP_BATTERY:
            return "battery";
        case FORM_FACTOR_CAP_WEARABLE:
            return "wearable";
        default:
            return "unknown";
    }
}

const char *form_factor_action_name(uint32_t action) {
    switch (action) {
        case FORM_FACTOR_ACTION_EXPRESSION:
            return "expression";
        case FORM_FACTOR_ACTION_VIDEO_DISPLAY:
            return "video_display";
        case FORM_FACTOR_ACTION_TOUCH:
            return "touch";
        case FORM_FACTOR_ACTION_WALK:
            return "walk";
        case FORM_FACTOR_ACTION_DANCE:
            return "dance";
        case FORM_FACTOR_ACTION_TAIL:
            return "tail";
        case FORM_FACTOR_ACTION_GESTURE:
            return "gesture";
        case FORM_FACTOR_ACTION_VOICE:
            return "voice";
        case FORM_FACTOR_ACTION_VIDEO_CALL:
            return "video_call";
        case FORM_FACTOR_ACTION_LOCATION:
            return "location";
        default:
            return "unknown";
    }
}

bool form_factor_has_capability(
    const form_factor_profile_t *profile,
    uint32_t capability
) {
    if (profile == NULL) {
        return false;
    }
    return (profile->capability_mask & capability) == capability &&
        capability != 0u;
}

bool form_factor_has_action(
    const form_factor_profile_t *profile,
    uint32_t action
) {
    if (profile == NULL) {
        return false;
    }
    return (profile->action_mask & action) == action && action != 0u;
}

const char *form_factor_error_name(form_factor_error_t error) {
    switch (error) {
        case FORM_FACTOR_OK:
            return "ok";
        case FORM_FACTOR_ERR_NULL_ARGUMENT:
            return "null_argument";
        case FORM_FACTOR_ERR_INVALID_PROFILE:
            return "invalid_profile";
        case FORM_FACTOR_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}
