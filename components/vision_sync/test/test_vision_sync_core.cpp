#include "vision_sync_core.h"

#include <cassert>
#include <cstring>

static vision_sync_result_t base_result() {
    vision_sync_result_t result = {};
    result.frame_width = 640;
    result.frame_height = 480;
    result.frame_bytes = 122880;
    std::strncpy(result.summary, "a red ball", sizeof(result.summary) - 1);
    result.label_count = 1;
    std::strncpy(result.labels[0].label, "ball", sizeof(result.labels[0].label) - 1);
    result.labels[0].confidence = 9000;
    return result;
}

int main() {
    // A well-formed bounded result is accepted.
    vision_sync_result_t valid = base_result();
    assert(vision_sync_core_result_is_valid(&valid));

    // A capture task may legitimately carry no labels.
    vision_sync_result_t capture = base_result();
    capture.label_count = 0;
    assert(vision_sync_core_result_is_valid(&capture));

    // A null result and an over-limit label set are rejected.
    assert(!vision_sync_core_result_is_valid(nullptr));
    vision_sync_result_t too_many = base_result();
    too_many.label_count = CAMERA_VISION_MAX_LABELS + 1;
    assert(!vision_sync_core_result_is_valid(&too_many));

    // Out-of-range frame metadata is rejected.
    vision_sync_result_t wide = base_result();
    wide.frame_width = 5000;
    assert(!vision_sync_core_result_is_valid(&wide));
    vision_sync_result_t tall = base_result();
    tall.frame_height = -1;
    assert(!vision_sync_core_result_is_valid(&tall));
    vision_sync_result_t huge = base_result();
    huge.frame_bytes = 9 * 1024 * 1024;
    assert(!vision_sync_core_result_is_valid(&huge));

    // An empty or unsafe label is rejected.
    vision_sync_result_t empty_label = base_result();
    empty_label.labels[0].label[0] = '\0';
    assert(!vision_sync_core_result_is_valid(&empty_label));
    vision_sync_result_t injected = base_result();
    std::strncpy(
        injected.labels[0].label,
        "bad\"label",
        sizeof(injected.labels[0].label) - 1
    );
    assert(!vision_sync_core_result_is_valid(&injected));

    // A confidence above the contract maximum is rejected.
    vision_sync_result_t over_confident = base_result();
    over_confident.labels[0].confidence = CAMERA_VISION_MAX_CONFIDENCE + 1;
    assert(!vision_sync_core_result_is_valid(&over_confident));

    // An over-long summary is rejected.
    vision_sync_result_t long_summary = base_result();
    std::memset(long_summary.summary, 'a', sizeof(long_summary.summary));
    assert(!vision_sync_core_result_is_valid(&long_summary));

    // Field copying rejects empty, missing and too-long values.
    char field[8] = {};
    assert(vision_sync_core_copy_field(field, sizeof(field), "task-12"));
    assert(std::strcmp(field, "task-12") == 0);
    assert(!vision_sync_core_copy_field(field, sizeof(field), "task-123"));
    assert(!vision_sync_core_copy_field(field, sizeof(field), ""));
    assert(!vision_sync_core_copy_field(field, sizeof(field), nullptr));
    assert(!vision_sync_core_copy_field(field, sizeof(field), "task-123456789"));

    // Status parsing accepts only the two device-visible states.
    vision_sync_task_status_t status = VISION_SYNC_TASK_PENDING;
    assert(vision_sync_core_status_from_name("pending", &status));
    assert(status == VISION_SYNC_TASK_PENDING);
    assert(vision_sync_core_status_from_name("dispatched", &status));
    assert(status == VISION_SYNC_TASK_DISPATCHED);
    assert(!vision_sync_core_status_from_name("completed", &status));
    assert(!vision_sync_core_status_from_name(nullptr, &status));

    // Failure codes stay inside the platform allow-list.
    assert(std::strcmp(
        vision_sync_failure_code_for_error(ESP_ERR_NOT_SUPPORTED),
        "unsupported"
    ) == 0);
    assert(std::strcmp(
        vision_sync_failure_code_for_error(ESP_ERR_TIMEOUT),
        "timeout"
    ) == 0);
    assert(std::strcmp(
        vision_sync_failure_code_for_error(ESP_ERR_INVALID_ARG),
        "invalid_frame"
    ) == 0);
    assert(std::strcmp(
        vision_sync_failure_code_for_error(ESP_ERR_INVALID_STATE),
        "capture_failed"
    ) == 0);
    assert(std::strcmp(
        vision_sync_failure_code_for_error(ESP_FAIL),
        "model_rejected"
    ) == 0);

    return 0;
}
