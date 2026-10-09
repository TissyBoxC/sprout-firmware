#include "ota_manager_event_sequence.h"

#include <string.h>

static int ota_event_sequence_index(const char *event_type) {
    static const char *const events[] = {
        "started",
        "downloading",
        "downloaded",
        "validated",
        "installing",
        "pending_verify",
        "succeeded",
        "failed",
        "rollback_started",
        "rolled_back",
    };
    if (event_type == NULL) {
        return -1;
    }
    for (size_t index = 0; index < sizeof(events) / sizeof(events[0]); ++index) {
        if (strcmp(event_type, events[index]) == 0) {
            return (int)index;
        }
    }
    return -1;
}

ota_event_sequence_error_t ota_event_sequence_append(
    const char **events,
    size_t event_count,
    size_t capacity,
    const char *event_type
) {
    if (events == NULL || capacity == 0 || event_type == NULL) {
        return OTA_EVENT_SEQUENCE_INVALID_ARGUMENT;
    }
    if (event_count >= capacity) {
        return OTA_EVENT_SEQUENCE_INVALID_TRANSITION;
    }
    const int next = ota_event_sequence_index(event_type);
    if (next < 0) {
        return OTA_EVENT_SEQUENCE_INVALID_ARGUMENT;
    }
    if (event_count == 0) {
        if (next != 0 && next != 6 && next != 7 && next != 9) {
            return OTA_EVENT_SEQUENCE_NOT_FIRST;
        }
        events[event_count] = event_type;
        return OTA_EVENT_SEQUENCE_OK;
    }
    const int previous = ota_event_sequence_index(events[event_count - 1]);
    if (previous < 0) {
        return OTA_EVENT_SEQUENCE_INVALID_TRANSITION;
    }
    bool allowed = next == previous + 1;
    if (previous == 5 && next == 7) {
        allowed = true;  // pending_verify -> failed is a valid install fault.
    }
    if (previous == 5 && next == 8) {
        allowed = true;  // pending_verify -> rollback_started is valid.
    }
    if (previous == 8 && next == 9) {
        allowed = true;  // rollback_started -> rolled_back is terminal.
    }
    if (!allowed) {
        return OTA_EVENT_SEQUENCE_INVALID_TRANSITION;
    }
    events[event_count] = event_type;
    return OTA_EVENT_SEQUENCE_OK;
}

bool ota_event_sequence_is_success(
    const char *const *events,
    size_t event_count
) {
    static const char *const expected[] = {
        "started",
        "downloading",
        "downloaded",
        "validated",
        "installing",
        "pending_verify",
        "succeeded",
    };
    if (events == NULL ||
        event_count != sizeof(expected) / sizeof(expected[0])) {
        return false;
    }
    for (size_t index = 0;
         index < sizeof(expected) / sizeof(expected[0]);
         ++index) {
        if (events[index] == NULL ||
            strcmp(events[index], expected[index]) != 0) {
            return false;
        }
    }
    return true;
}

bool ota_event_sequence_is_terminal(
    const char *const *events,
    size_t event_count
) {
    if (events == NULL || event_count == 0 || events[event_count - 1] == NULL) {
        return false;
    }
    return strcmp(events[event_count - 1], "succeeded") == 0 ||
        strcmp(events[event_count - 1], "failed") == 0 ||
        strcmp(events[event_count - 1], "rolled_back") == 0;
}
