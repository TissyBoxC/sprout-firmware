// Host test for the camera vision runtime: guardian consent gating and the
// bounded task lifecycle, exercised with the real privacy_guard implementation.

#include "camera_vision.h"
#include "privacy_guard.h"

#include <cassert>
#include <cstring>

static bool test_guard_ready;

static esp_err_t test_load(
    bool *audio_upload_consent,
    bool *image_upload_consent,
    bool *conversation_history_consent,
    bool *usage_analytics_consent,
    uint32_t *consent_revision
) {
    *audio_upload_consent = false;
    *image_upload_consent = false;
    *conversation_history_consent = false;
    *usage_analytics_consent = false;
    *consent_revision = 0;
    return ESP_OK;
}

static esp_err_t test_save(
    bool audio_upload_consent,
    bool image_upload_consent,
    bool conversation_history_consent,
    bool usage_analytics_consent,
    uint32_t consent_revision
) {
    (void)audio_upload_consent;
    (void)image_upload_consent;
    (void)conversation_history_consent;
    (void)usage_analytics_consent;
    (void)consent_revision;
    return ESP_OK;
}

static esp_err_t test_clear_cache(privacy_guard_action_t action) {
    (void)action;
    return ESP_OK;
}

static const privacy_guard_storage_t test_storage = {
    .load = test_load,
    .save = test_save,
    .clear_cache = test_clear_cache,
};

static void ensure_guard(void) {
    if (!test_guard_ready) {
        test_guard_ready =
            privacy_guard_init_with_storage(&test_storage) == ESP_OK;
    }
    assert(test_guard_ready);
}

static void grant_consent(bool granted) {
    if (granted) {
        assert(
            privacy_guard_grant_consent(PRIVACY_GUARD_DATA_IMAGE_UPLOAD) ==
            PRIVACY_GUARD_OK
        );
    } else {
        assert(
            privacy_guard_revoke_consent(PRIVACY_GUARD_DATA_IMAGE_UPLOAD) ==
            PRIVACY_GUARD_OK
        );
    }
}

static void test_task_requires_consent() {
    ensure_guard();
    grant_consent(false);
    assert(camera_vision_init() == ESP_OK);
    assert(
        camera_vision_accept_task(
            "task_00000001",
            CAMERA_VISION_TASK_OBJECT_RECOGNITION
        ) == CAMERA_VISION_ERR_PRIVACY_CONSENT_REQUIRED
    );
}

static void test_task_lifecycle_with_consent() {
    ensure_guard();
    grant_consent(true);
    assert(camera_vision_init() == ESP_OK);
    assert(
        camera_vision_accept_task(
            "task_00000002",
            CAMERA_VISION_TASK_PICTURE_BOOK_RECOGNITION
        ) == CAMERA_VISION_OK
    );
    const camera_vision_snapshot_t accepted = camera_vision_get_snapshot();
    assert(accepted.sequence >= 1);
    assert(
        std::strcmp(accepted.task_id, "task_00000002") == 0
    );
    assert(camera_vision_acknowledge() == CAMERA_VISION_OK);
    assert(
        camera_vision_get_snapshot().state ==
        CAMERA_VISION_STATE_ACKNOWLEDGED
    );

    camera_vision_label_t label = {};
    std::strncpy(label.label, "picture book", CAMERA_VISION_MAX_LABEL_RUNES);
    label.confidence = 8000;
    char output[256] = {};
    assert(
        camera_vision_encode_result(&label, 1, "a book", output, sizeof(output)) >
        0
    );
    assert(std::strstr(output, "task_00000002") != nullptr);
    assert(camera_vision_complete() == CAMERA_VISION_OK);
}

static void test_revocation_stops_next_task() {
    ensure_guard();
    grant_consent(true);
    assert(
        camera_vision_accept_task(
            "task_00000003",
            CAMERA_VISION_TASK_PHOTO_QA
        ) == CAMERA_VISION_OK
    );
    assert(camera_vision_acknowledge() == CAMERA_VISION_OK);
    assert(camera_vision_complete() == CAMERA_VISION_OK);
    grant_consent(false);
    assert(
        camera_vision_accept_task(
            "task_00000004",
            CAMERA_VISION_TASK_PHOTO_QA
        ) == CAMERA_VISION_ERR_PRIVACY_CONSENT_REQUIRED
    );
}

static void test_failure_is_counted() {
    ensure_guard();
    grant_consent(true);
    const uint32_t before = camera_vision_get_snapshot().failure_count;
    assert(camera_vision_fail("capture_failed") == CAMERA_VISION_OK);
    assert(camera_vision_get_snapshot().failure_count == before + 1);
}

int main() {
    test_task_requires_consent();
    test_task_lifecycle_with_consent();
    test_revocation_stops_next_task();
    test_failure_is_counted();
    return 0;
}
