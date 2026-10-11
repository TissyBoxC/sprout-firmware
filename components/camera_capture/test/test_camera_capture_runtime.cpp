// Host test for the camera capture runtime, exercising the guardian-consent
// gate with the real privacy_guard implementation and in-memory storage.

#include "camera_capture.h"
#include "privacy_guard.h"

#include <cassert>

static bool test_image_consent;
static bool test_guard_ready;

static esp_err_t test_load(
    bool *audio_upload_consent,
    bool *image_upload_consent,
    bool *conversation_history_consent,
    bool *usage_analytics_consent,
    uint32_t *consent_revision
) {
    *audio_upload_consent = false;
    *image_upload_consent = test_image_consent;
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
    (void)conversation_history_consent;
    (void)usage_analytics_consent;
    (void)consent_revision;
    test_image_consent = image_upload_consent;
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

static void reset_guard(bool image_consent) {
    test_image_consent = image_consent;
    if (!test_guard_ready) {
        test_guard_ready =
            privacy_guard_init_with_storage(&test_storage) == ESP_OK;
    } else if (image_consent) {
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
    assert(test_guard_ready);
}

static void test_capture_requires_consent() {
    reset_guard(false);
    assert(camera_capture_init() == ESP_OK);
    assert(camera_capture_is_ready());
    assert(camera_capture_start() == CAMERA_CAPTURE_ERR_PRIVACY_CONSENT_REQUIRED);
    assert(!camera_capture_get_snapshot().guardian_consent);
    assert(camera_capture_finish(320, 240, 2048) == CAMERA_CAPTURE_ERR_BUSY);
}

static void test_capture_lifecycle_with_consent() {
    reset_guard(true);
    assert(camera_capture_init() == ESP_OK);
    assert(camera_capture_start() == CAMERA_CAPTURE_OK);
    assert(camera_capture_get_snapshot().state == CAMERA_CAPTURE_CAPTURING);
    assert(camera_capture_finish(320, 240, 2048) == CAMERA_CAPTURE_OK);
    const camera_capture_snapshot_t snapshot = camera_capture_get_snapshot();
    assert(snapshot.state == CAMERA_CAPTURE_READY);
    assert(snapshot.sequence >= 1);
    assert(snapshot.width == 320);
    assert(snapshot.height == 240);
    assert(snapshot.byte_size == 2048);
    assert(snapshot.guardian_consent);
}

static void test_revocation_stops_next_frame() {
    reset_guard(true);
    assert(camera_capture_init() == ESP_OK);
    assert(camera_capture_start() == CAMERA_CAPTURE_OK);
    assert(camera_capture_finish(320, 240, 2048) == CAMERA_CAPTURE_OK);

    assert(
        privacy_guard_revoke_consent(PRIVACY_GUARD_DATA_IMAGE_UPLOAD) ==
        PRIVACY_GUARD_OK
    );
    assert(camera_capture_start() == CAMERA_CAPTURE_ERR_PRIVACY_CONSENT_REQUIRED);
    assert(!camera_capture_get_snapshot().guardian_consent);
}

static void test_invalid_frame_is_dropped() {
    reset_guard(true);
    assert(camera_capture_init() == ESP_OK);
    const uint32_t before = camera_capture_get_snapshot().dropped_frames;
    assert(camera_capture_start() == CAMERA_CAPTURE_OK);
    assert(camera_capture_finish(1, 1, 0) == CAMERA_CAPTURE_ERR_INVALID_FRAME);
    const camera_capture_snapshot_t snapshot = camera_capture_get_snapshot();
    assert(snapshot.dropped_frames == before + 1);
    assert(snapshot.state == CAMERA_CAPTURE_READY);
}

int main() {
    test_capture_requires_consent();
    test_capture_lifecycle_with_consent();
    test_revocation_stops_next_frame();
    test_invalid_frame_is_dropped();
    return 0;
}
