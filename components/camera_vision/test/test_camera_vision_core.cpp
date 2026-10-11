#include <cassert>
#include <cstring>

#include "camera_vision_core.h"

static camera_vision_label_t make_label(const char *text, uint32_t confidence) {
    camera_vision_label_t label = {};
    std::strncpy(label.label, text, CAMERA_VISION_MAX_LABEL_RUNES);
    label.confidence = confidence;
    return label;
}

static void test_task_id_validation() {
    assert(camera_vision_core_task_id_is_valid("task_00000001"));
    assert(!camera_vision_core_task_id_is_valid("short"));
    assert(!camera_vision_core_task_id_is_valid(nullptr));
}

static void test_label_validation() {
    assert(camera_vision_core_label_is_valid("teddy bear"));
    assert(!camera_vision_core_label_is_valid(""));
    assert(!camera_vision_core_label_is_valid(nullptr));
    assert(!camera_vision_core_label_is_valid("quote\"injection"));
}

static void test_task_type_names() {
    assert(
        std::strcmp(
            camera_vision_core_task_type_name(
                CAMERA_VISION_TASK_OBJECT_RECOGNITION
            ),
            "object_recognition"
        ) == 0
    );
    assert(
        camera_vision_core_task_type_name(CAMERA_VISION_TASK_COUNT) == nullptr
    );
}

static void test_encode_result() {
    camera_vision_label_t labels[2] = {
        make_label("teddy bear", 9500),
        make_label("book", 12000),
    };
    char output[256] = {};
    const int length = camera_vision_core_encode_result(
        "task_00000001",
        labels,
        2,
        "a toy",
        output,
        sizeof(output)
    );
    assert(length > 0);
    assert(
        std::strstr(output, "\"task_id\":\"task_00000001\"") != nullptr
    );
    assert(std::strstr(output, "\"label\":\"teddy bear\"") != nullptr);
    // Confidence above the contract maximum is clamped to 10000.
    assert(std::strstr(output, "\"confidence\":10000") != nullptr);
}

static void test_encode_rejects_invalid_input() {
    camera_vision_label_t label = make_label("ok", 100);
    char small[8] = {};
    assert(
        camera_vision_core_encode_result(
            "task_00000001",
            &label,
            1,
            "",
            small,
            sizeof(small)
        ) == -CAMERA_VISION_ERR_BUSY
    );
    camera_vision_label_t many[CAMERA_VISION_MAX_LABELS + 1] = {};
    char output[4096] = {};
    assert(
        camera_vision_core_encode_result(
            "task_00000001",
            many,
            CAMERA_VISION_MAX_LABELS + 1,
            "",
            output,
            sizeof(output)
        ) == -CAMERA_VISION_ERR_TOO_MANY_LABELS
    );
}

int main() {
    test_task_id_validation();
    test_label_validation();
    test_task_type_names();
    test_encode_result();
    test_encode_rejects_invalid_input();
    return 0;
}
