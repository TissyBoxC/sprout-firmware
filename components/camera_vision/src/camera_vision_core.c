#include "camera_vision_core.h"

#include <string.h>

#define CAMERA_VISION_CORE_MAX_TASK_ID 63u
#define CAMERA_VISION_CORE_MAX_SUMMARY 200u

static bool camera_vision_core_char_is_safe(char value) {
    return (value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9') ||
        value == ' ' || value == '.' || value == ':' ||
        value == '/' || value == '-' || value == '_';
}

bool camera_vision_core_task_id_is_valid(const char *task_id) {
    if (task_id == NULL) {
        return false;
    }
    const size_t length = strnlen(task_id, CAMERA_VISION_CORE_MAX_TASK_ID + 1);
    if (length < 8u || length > CAMERA_VISION_CORE_MAX_TASK_ID) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        if (!camera_vision_core_char_is_safe(task_id[index])) {
            return false;
        }
    }
    return true;
}

bool camera_vision_core_label_is_valid(const char *label) {
    if (label == NULL) {
        return false;
    }
    const size_t length = strnlen(
        label,
        CAMERA_VISION_MAX_LABEL_RUNES + 1
    );
    if (length == 0u || length > CAMERA_VISION_MAX_LABEL_RUNES) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        if (!camera_vision_core_char_is_safe(label[index])) {
            return false;
        }
    }
    return true;
}

const char *camera_vision_core_task_type_name(
    camera_vision_task_type_t task_type
) {
    switch (task_type) {
        case CAMERA_VISION_TASK_OBJECT_RECOGNITION:
            return "object_recognition";
        case CAMERA_VISION_TASK_PICTURE_BOOK_RECOGNITION:
            return "picture_book_recognition";
        case CAMERA_VISION_TASK_PHOTO_QA:
            return "photo_qa";
        case CAMERA_VISION_TASK_CAPTURE:
            return "capture";
        default:
            return NULL;
    }
}

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool overflow;
} camera_vision_writer_t;

static void camera_vision_writer_char(
    camera_vision_writer_t *writer,
    char value
) {
    if (writer->length + 1u >= writer->capacity) {
        writer->overflow = true;
        return;
    }
    writer->buffer[writer->length] = value;
    writer->length += 1u;
    writer->buffer[writer->length] = '\0';
}

static void camera_vision_writer_raw(
    camera_vision_writer_t *writer,
    const char *value
) {
    if (value == NULL) {
        return;
    }
    for (size_t index = 0; value[index] != '\0'; ++index) {
        camera_vision_writer_char(writer, value[index]);
    }
}

static void camera_vision_writer_uint(
    camera_vision_writer_t *writer,
    uint32_t value
) {
    char digits[10];
    size_t count = 0u;
    if (value == 0u) {
        camera_vision_writer_char(writer, '0');
        return;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count] = (char)('0' + (value % 10u));
        value /= 10u;
        count += 1u;
    }
    while (count > 0u) {
        count -= 1u;
        camera_vision_writer_char(writer, digits[count]);
    }
}

static void camera_vision_writer_string(
    camera_vision_writer_t *writer,
    const char *value
) {
    camera_vision_writer_char(writer, '"');
    camera_vision_writer_raw(writer, value);
    camera_vision_writer_char(writer, '"');
}

int camera_vision_core_encode_result(
    const char *task_id,
    const camera_vision_label_t *labels,
    size_t label_count,
    const char *summary,
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size == 0u) {
        return -CAMERA_VISION_ERR_NULL_ARGUMENT;
    }
    output[0] = '\0';
    if (!camera_vision_core_task_id_is_valid(task_id)) {
        return -CAMERA_VISION_ERR_NULL_ARGUMENT;
    }
    if (label_count > CAMERA_VISION_MAX_LABELS) {
        return -CAMERA_VISION_ERR_TOO_MANY_LABELS;
    }
    if (labels == NULL && label_count > 0u) {
        return -CAMERA_VISION_ERR_NULL_ARGUMENT;
    }
    for (size_t index = 0; index < label_count; ++index) {
        if (!camera_vision_core_label_is_valid(labels[index].label)) {
            return -CAMERA_VISION_ERR_INVALID_LABEL;
        }
    }
    if (summary != NULL &&
        strnlen(summary, CAMERA_VISION_CORE_MAX_SUMMARY + 1) >
            CAMERA_VISION_CORE_MAX_SUMMARY) {
        return -CAMERA_VISION_ERR_INVALID_LABEL;
    }
    if (summary != NULL) {
        for (const char *cursor = summary; *cursor != '\0'; ++cursor) {
            if (!camera_vision_core_char_is_safe(*cursor)) {
                return -CAMERA_VISION_ERR_INVALID_LABEL;
            }
        }
    }

    camera_vision_writer_t writer = {
        .buffer = output,
        .capacity = output_size,
        .length = 0u,
        .overflow = false,
    };
    camera_vision_writer_raw(&writer, "{\"task_id\":");
    camera_vision_writer_string(&writer, task_id);
    camera_vision_writer_raw(&writer, ",\"labels\":[");
    for (size_t index = 0; index < label_count; ++index) {
        if (index > 0u) {
            camera_vision_writer_char(&writer, ',');
        }
        camera_vision_writer_raw(&writer, "{\"label\":");
        camera_vision_writer_string(&writer, labels[index].label);
        camera_vision_writer_raw(&writer, ",\"confidence\":");
        uint32_t confidence = labels[index].confidence;
        if (confidence > CAMERA_VISION_MAX_CONFIDENCE) {
            confidence = CAMERA_VISION_MAX_CONFIDENCE;
        }
        camera_vision_writer_uint(&writer, confidence);
        camera_vision_writer_char(&writer, '}');
    }
    camera_vision_writer_raw(&writer, "],\"summary\":");
    camera_vision_writer_string(&writer, summary == NULL ? "" : summary);
    camera_vision_writer_char(&writer, '}');

    if (writer.overflow) {
        output[0] = '\0';
        return -CAMERA_VISION_ERR_BUSY;
    }
    return (int)writer.length;
}
