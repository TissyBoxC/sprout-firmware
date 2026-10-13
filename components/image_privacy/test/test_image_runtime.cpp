#include "image_privacy.h"

#include <cassert>

int main() {
    assert(image_privacy_init() == ESP_OK);
    assert(image_privacy_is_ready());
    // The default policy disables the feature, so processing and upload fail.
    assert(!image_privacy_processing_allowed());
    assert(image_privacy_begin_processing() ==
           IMAGE_PRIVACY_ERR_PERMISSION_DENIED);
    assert(image_privacy_last_block_reason() == IMAGE_BLOCK_DISABLED);
    assert(image_privacy_processed_count() == 0u);
    assert(image_privacy_uploaded_count() == 0u);
    assert(image_privacy_blocked_count() == 1u);

    // Enable local-full processing and authorise thumbnail upload.
    image_privacy_policy_t policy = image_privacy_get();
    policy.enabled = 1u;
    policy.local_processing_only = 0u;
    policy.upload_allowed = 1u;
    policy.processing_mode = (uint8_t)IMAGE_PROCESSING_LOCAL_FULL;
    policy.upload_scope = (uint8_t)IMAGE_UPLOAD_THUMBNAILS;
    assert(image_privacy_set(&policy) == ESP_OK);
    assert(image_privacy_processing_allowed());

    // Processing counts a frame, and the guardian scope is enforced.
    assert(image_privacy_begin_processing() == ESP_OK);
    assert(image_privacy_processed_count() == 1u);
    assert(image_privacy_authorize_upload(IMAGE_UPLOAD_ANALYSIS) ==
           IMAGE_PRIVACY_ERR_PERMISSION_DENIED);
    assert(image_privacy_last_block_reason() == IMAGE_BLOCK_SCOPE);
    assert(image_privacy_authorize_upload(IMAGE_UPLOAD_THUMBNAILS) == ESP_OK);
    assert(image_privacy_record_upload() == ESP_OK);
    assert(image_privacy_uploaded_count() == 1u);
    // The uploaded count can never exceed the processed count.
    assert(image_privacy_record_upload() ==
           IMAGE_PRIVACY_ERR_INVALID_STATE);

    // A bounded explicit block reason is recorded.
    assert(image_privacy_block(IMAGE_BLOCK_QUOTA) == ESP_OK);
    assert(image_privacy_last_block_reason() == IMAGE_BLOCK_QUOTA);
    assert(image_privacy_block(IMAGE_BLOCK_NONE) == ESP_ERR_INVALID_ARG);

    // The status snapshot mirrors the live pipeline and the applied revision.
    policy = image_privacy_get();
    policy.revision = 9u;
    assert(image_privacy_set(&policy) == ESP_OK);
    image_privacy_status_t status = image_privacy_status();
    assert(status.camera_supported);
    assert(status.local_processing);
    assert(status.upload_allowed);
    assert(status.processing_mode == IMAGE_PROCESSING_LOCAL_FULL);
    assert(status.upload_scope == IMAGE_UPLOAD_THUMBNAILS);
    assert(status.processed_count == 1u);
    assert(status.uploaded_count == 1u);
    assert(status.blocked_count == 3u);
    assert(status.last_block_reason == IMAGE_BLOCK_QUOTA);
    assert(status.revision == 9u);

    // Local-only processing revokes upload in one step.
    policy = image_privacy_get();
    policy.local_processing_only = 1u;
    policy.upload_allowed = 1u;
    policy.upload_scope = (uint8_t)IMAGE_UPLOAD_ANALYSIS;
    assert(image_privacy_set(&policy) == ESP_OK);
    policy = image_privacy_get();
    assert(image_privacy_can_process(&policy));
    assert(image_privacy_get().upload_allowed == 0u);
    assert(image_privacy_get().upload_scope == (uint8_t)IMAGE_UPLOAD_NONE);
    assert(image_privacy_authorize_upload(IMAGE_UPLOAD_ANALYSIS) ==
           IMAGE_PRIVACY_ERR_PERMISSION_DENIED);

    // Disabling the feature stops processing immediately.
    assert(image_privacy_reset_counters() == ESP_OK);
    assert(image_privacy_processed_count() == 0u);
    policy = image_privacy_get();
    policy.enabled = 0u;
    assert(image_privacy_set(&policy) == ESP_OK);
    assert(!image_privacy_processing_allowed());
    assert(image_privacy_begin_processing() ==
           IMAGE_PRIVACY_ERR_PERMISSION_DENIED);
    assert(image_privacy_last_block_reason() == IMAGE_BLOCK_DISABLED);
    // A disabled policy reports a fully off pipeline with no counters.
    image_privacy_status_t disabled = image_privacy_status();
    assert(!disabled.local_processing);
    assert(!disabled.upload_allowed);
    assert(disabled.processing_mode == IMAGE_PROCESSING_OFF);
    assert(disabled.upload_scope == IMAGE_UPLOAD_NONE);
    assert(disabled.processed_count == 0u);
    assert(disabled.uploaded_count == 0u);
    assert(disabled.blocked_count == 0u);
    assert(disabled.last_block_reason == IMAGE_BLOCK_NONE);

    // Persistence round trip keeps the edited policy.
    image_privacy_policy_t persisted = image_privacy_default();
    persisted.enabled = 1u;
    persisted.local_processing_only = 0u;
    persisted.upload_allowed = 1u;
    persisted.processing_mode = (uint8_t)IMAGE_PROCESSING_METADATA;
    persisted.upload_scope = (uint8_t)IMAGE_UPLOAD_ANALYSIS;
    persisted.retention_days = 14u;
    persisted.redaction_level = (uint8_t)IMAGE_REDACTION_STANDARD;
    assert(image_privacy_set(&persisted) == ESP_OK);
    assert(image_privacy_init() == ESP_OK);
    assert(image_privacy_get().enabled == 1u);
    assert(image_privacy_get().upload_allowed == 1u);
    assert(image_privacy_get().processing_mode ==
           (uint8_t)IMAGE_PROCESSING_METADATA);
    assert(image_privacy_get().upload_scope ==
           (uint8_t)IMAGE_UPLOAD_ANALYSIS);
    assert(image_privacy_get().retention_days == 14u);
    assert(image_privacy_get().redaction_level ==
           (uint8_t)IMAGE_REDACTION_STANDARD);

    // A policy that is not version-compatible is refused by apply.
    image_privacy_policy_t invalid = image_privacy_default();
    invalid.schema_version = 42u;
    assert(image_privacy_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(image_privacy_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor = image_privacy_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
