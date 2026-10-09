#include "device_message_core.h"

#include <string.h>

size_t device_message_utf8_runes(const char *value) {
    if (value == NULL) {
        return 0;
    }
    size_t runes = 0;
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != '\0';
         ++cursor) {
        if ((*cursor & 0xC0) != 0x80) {
            ++runes;
        }
    }
    return runes;
}

bool device_message_title_is_valid(const char *title) {
    if (title == NULL || title[0] == '\0') {
        return false;
    }
    return device_message_utf8_runes(title) <= DEVICE_MESSAGE_TITLE_MAX_RUNES;
}

bool device_message_body_is_valid(const char *body) {
    if (body == NULL) {
        return false;
    }
    return device_message_utf8_runes(body) <= DEVICE_MESSAGE_BODY_MAX_RUNES;
}

bool device_message_severity_is_valid(const char *severity) {
    return severity != NULL &&
        (strcmp(severity, "info") == 0 ||
         strcmp(severity, "success") == 0 ||
         strcmp(severity, "warning") == 0 ||
         strcmp(severity, "critical") == 0);
}

uint32_t device_message_clamp_duration(uint32_t requested_seconds) {
    if (requested_seconds < DEVICE_MESSAGE_MIN_DURATION_SECONDS ||
        requested_seconds > DEVICE_MESSAGE_MAX_DURATION_SECONDS) {
        return DEVICE_MESSAGE_DEFAULT_DURATION_SECONDS;
    }
    return requested_seconds;
}
