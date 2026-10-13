#include "vision_sync_core.h"

#include <string.h>

#include "camera_capture.h"
#include "esp_err.h"

// The platform bounds a task id at 128 bytes; the firmware contract keeps a
// tighter 63-byte identifier so it always fits the snapshot struct.
#define VISION_SYNC_CORE_MAX_TASK_ID 63u
#define VISION_SYNC_CORE_MAX_SUMMARY 200u

bool vision_sync_core_copy_field(
    char *destination,
    size_t destination_size,
    const char *value
) {
    if (destination == NULL || destination_size == 0u) {
        return false;
    }
    destination[0] = '\0';
    if (value == NULL) {
        return false;
    }
    const size_t length = strlen(value);
    if (length == 0u || length >= destination_size) {
        return false;
    }
    memcpy(destination, value, length + 1u);
    return true;
}

bool vision_sync_core_status_from_name(
    const char *name,
    vision_sync_task_status_t *status_out
) {
    if (name == NULL || status_out == NULL) {
        return false;
    }
    if (strcmp(name, "pending") == 0) {
        *status_out = VISION_SYNC_TASK_PENDING;
        return true;
    }
    if (strcmp(name, "dispatched") == 0) {
        *status_out = VISION_SYNC_TASK_DISPATCHED;
        return true;
    }
    return false;
}

bool vision_sync_core_result_is_valid(const vision_sync_result_t *result) {
    if (result == NULL) {
        return false;
    }
    if (result->label_count > CAMERA_VISION_MAX_LABELS) {
        return false;
    }
    if (result->frame_width < 0 ||
        result->frame_width > (int)CAMERA_CAPTURE_MAX_DIMENSION) {
        return false;
    }
    if (result->frame_height < 0 ||
        result->frame_height > (int)CAMERA_CAPTURE_MAX_DIMENSION) {
        return false;
    }
    if (result->frame_bytes < 0 ||
        result->frame_bytes > (int)CAMERA_CAPTURE_MAX_FRAME_BYTES) {
        return false;
    }
    if (strnlen(result->summary, VISION_SYNC_CORE_MAX_SUMMARY + 1u) >
        VISION_SYNC_CORE_MAX_SUMMARY) {
        return false;
    }
    for (size_t index = 0; index < result->label_count; ++index) {
        const char *label = result->labels[index].label;
        const size_t length = strnlen(label, CAMERA_VISION_MAX_LABEL_RUNES + 1u);
        if (length == 0u || length > CAMERA_VISION_MAX_LABEL_RUNES) {
            return false;
        }
        for (size_t rune = 0; rune < length; ++rune) {
            const char value = label[rune];
            const bool safe =
                (value >= 'A' && value <= 'Z') ||
                (value >= 'a' && value <= 'z') ||
                (value >= '0' && value <= '9') ||
                value == ' ' || value == '.' || value == ':' ||
                value == '/' || value == '-' || value == '_';
            if (!safe) {
                return false;
            }
        }
        if (result->labels[index].confidence > CAMERA_VISION_MAX_CONFIDENCE) {
            return false;
        }
    }
    return true;
}

const char *vision_sync_failure_code_for_error(esp_err_t error) {
    switch (error) {
        case ESP_ERR_NOT_SUPPORTED:
            return "unsupported";
        case ESP_ERR_TIMEOUT:
            return "timeout";
        case ESP_ERR_INVALID_ARG:
            return "invalid_frame";
        case ESP_ERR_INVALID_STATE:
            return "capture_failed";
        default:
            return "model_rejected";
    }
}
