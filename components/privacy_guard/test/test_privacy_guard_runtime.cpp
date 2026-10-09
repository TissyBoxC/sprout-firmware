// Host test for the privacy guard runtime with an in-memory storage backend.

#include "privacy_guard.h"

#include <cassert>
#include <cstring>

typedef struct {
    bool audio;
    bool image;
    bool conversation;
    bool analytics;
    uint32_t revision;
    int save_count;
    int clear_count;
    bool fail_save;
    bool fail_clear;
} privacy_guard_test_storage_t;

static privacy_guard_test_storage_t test_storage;

static esp_err_t test_load(
    bool *audio_upload_consent,
    bool *image_upload_consent,
    bool *conversation_history_consent,
    bool *usage_analytics_consent,
    uint32_t *consent_revision
) {
    *audio_upload_consent = test_storage.audio;
    *image_upload_consent = test_storage.image;
    *conversation_history_consent = test_storage.conversation;
    *usage_analytics_consent = test_storage.analytics;
    *consent_revision = test_storage.revision;
    return ESP_OK;
}

static esp_err_t test_save(
    bool audio_upload_consent,
    bool image_upload_consent,
    bool conversation_history_consent,
    bool usage_analytics_consent,
    uint32_t consent_revision
) {
    ++test_storage.save_count;
    if (test_storage.fail_save) {
        return ESP_FAIL;
    }
    test_storage.audio = audio_upload_consent;
    test_storage.image = image_upload_consent;
    test_storage.conversation = conversation_history_consent;
    test_storage.analytics = usage_analytics_consent;
    test_storage.revision = consent_revision;
    return ESP_OK;
}

static esp_err_t test_clear(privacy_guard_action_t action) {
    (void)action;
    ++test_storage.clear_count;
    return test_storage.fail_clear ? ESP_FAIL : ESP_OK;
}

static privacy_guard_storage_t storage(void) {
    return (privacy_guard_storage_t){
        .load = test_load,
        .save = test_save,
        .clear_cache = test_clear,
    };
}

static void reset_storage(void) {
    test_storage = (privacy_guard_test_storage_t){};
}

static void test_defaults_are_denied(void) {
    reset_storage();
    const privacy_guard_storage_t configured = storage();
    assert(privacy_guard_init_with_storage(&configured) == ESP_OK);
    assert(privacy_guard_is_ready());
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_AUDIO_UPLOAD));
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_IMAGE_UPLOAD));
    assert(!privacy_guard_has_consent(
        PRIVACY_GUARD_DATA_CONVERSATION_HISTORY
    ));
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_USAGE_ANALYTICS));
}

static void test_evaluate_denies_without_consent(void) {
    const privacy_guard_storage_t configured = storage();
    assert(privacy_guard_init_with_storage(&configured) == ESP_OK);
    privacy_guard_decision_t decision = {};
    assert(privacy_guard_evaluate(
        PRIVACY_GUARD_DATA_AUDIO_UPLOAD,
        &decision
    ) == PRIVACY_GUARD_ERR_CONSENT_REQUIRED);
    assert(!decision.allowed);
    assert(!decision.guardian_consent);
    assert(decision.reason == PRIVACY_GUARD_ERR_CONSENT_REQUIRED);
}

static void test_grant_and_revoke(void) {
    const privacy_guard_storage_t configured = storage();
    assert(privacy_guard_init_with_storage(&configured) == ESP_OK);
    assert(privacy_guard_grant_consent(
        PRIVACY_GUARD_DATA_AUDIO_UPLOAD
    ) == PRIVACY_GUARD_OK);
    assert(privacy_guard_has_consent(PRIVACY_GUARD_DATA_AUDIO_UPLOAD));
    assert(test_storage.audio);
    assert(test_storage.revision == 1);

    privacy_guard_decision_t decision = {};
    assert(privacy_guard_evaluate(
        PRIVACY_GUARD_DATA_AUDIO_UPLOAD,
        &decision
    ) == PRIVACY_GUARD_OK);
    assert(decision.allowed);
    assert(decision.guardian_consent);

    assert(privacy_guard_revoke_consent(
        PRIVACY_GUARD_DATA_AUDIO_UPLOAD
    ) == PRIVACY_GUARD_OK);
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_AUDIO_UPLOAD));
    assert(!test_storage.audio);
}

static void test_revoke_all(void) {
    const privacy_guard_storage_t configured = storage();
    assert(privacy_guard_init_with_storage(&configured) == ESP_OK);
    assert(privacy_guard_grant_consent(
        PRIVACY_GUARD_DATA_AUDIO_UPLOAD
    ) == PRIVACY_GUARD_OK);
    assert(privacy_guard_grant_consent(
        PRIVACY_GUARD_DATA_IMAGE_UPLOAD
    ) == PRIVACY_GUARD_OK);
    assert(privacy_guard_grant_consent(
        PRIVACY_GUARD_DATA_CONVERSATION_HISTORY
    ) == PRIVACY_GUARD_OK);
    assert(privacy_guard_grant_consent(
        PRIVACY_GUARD_DATA_USAGE_ANALYTICS
    ) == PRIVACY_GUARD_OK);
    assert(privacy_guard_revoke_all() == PRIVACY_GUARD_OK);
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_AUDIO_UPLOAD));
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_IMAGE_UPLOAD));
    assert(!privacy_guard_has_consent(
        PRIVACY_GUARD_DATA_CONVERSATION_HISTORY
    ));
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_USAGE_ANALYTICS));
}

static void test_clear_local_data(void) {
    const privacy_guard_storage_t configured = storage();
    assert(privacy_guard_init_with_storage(&configured) == ESP_OK);
    assert(privacy_guard_clear_local_data(
        PRIVACY_GUARD_ACTION_UNBIND
    ) == PRIVACY_GUARD_OK);
    assert(test_storage.clear_count == 1);
    assert(privacy_guard_local_deletion_count() == 1);

    test_storage.fail_clear = true;
    assert(privacy_guard_clear_local_data(
        PRIVACY_GUARD_ACTION_FACTORY_RESET
    ) == PRIVACY_GUARD_ERR_STORAGE);
    assert(privacy_guard_local_deletion_count() == 1);
}

static void test_invalid_inputs(void) {
    const privacy_guard_storage_t configured = storage();
    assert(privacy_guard_init_with_storage(&configured) == ESP_OK);
    assert(privacy_guard_evaluate(
        (privacy_guard_data_class_t)99,
        nullptr
    ) == PRIVACY_GUARD_ERR_NULL_ARGUMENT);
    assert(privacy_guard_grant_consent(
        (privacy_guard_data_class_t)99
    ) == PRIVACY_GUARD_ERR_INVALID_CLASS);
    assert(std::strcmp(
        privacy_guard_data_class_name(
            PRIVACY_GUARD_DATA_IMAGE_UPLOAD
        ),
        "image_upload"
    ) == 0);
    assert(std::strcmp(
        privacy_guard_error_name(
            PRIVACY_GUARD_ERR_CONSENT_REQUIRED
        ),
        "guardian_consent_required"
    ) == 0);
}

int main(void) {
    test_defaults_are_denied();
    test_evaluate_denies_without_consent();
    test_grant_and_revoke();
    test_revoke_all();
    test_clear_local_data();
    test_invalid_inputs();
    return 0;
}
