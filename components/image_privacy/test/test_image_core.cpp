#include "image_privacy.h"

#include <cassert>
#include <cstring>

int main() {
    const image_privacy_policy_t defaults = image_privacy_default();
    assert(image_privacy_is_valid(&defaults));
    assert(defaults.enabled == 0u);
    assert(defaults.upload_allowed == 0u);
    assert(defaults.local_processing_only == 1u);
    assert(defaults.processing_mode == (uint8_t)IMAGE_PROCESSING_METADATA);
    assert(defaults.upload_scope == (uint8_t)IMAGE_UPLOAD_NONE);
    assert(defaults.retention_days == IMAGE_PRIVACY_MIN_RETENTION_DAYS);
    assert(defaults.redaction_level == (uint8_t)IMAGE_REDACTION_STRICT);
    assert(defaults.require_child_consent == 1u);
    // The default policy is opt-in, so it cannot process or upload yet.
    assert(!image_privacy_can_process(&defaults));
    assert(!image_privacy_can_upload(&defaults));

    image_privacy_policy_t out_of_range = defaults;
    out_of_range.enabled = 1u;
    out_of_range.local_processing_only = 0u;
    out_of_range.upload_allowed = 1u;
    out_of_range.processing_mode = 200u;
    out_of_range.upload_scope = 200u;
    out_of_range.retention_days = 250u;
    out_of_range.redaction_level = 200u;
    const image_privacy_policy_t clamped = image_privacy_clamp(&out_of_range);
    assert(clamped.processing_mode == (uint8_t)IMAGE_PROCESSING_LOCAL_FULL);
    assert(clamped.upload_scope == (uint8_t)IMAGE_UPLOAD_ANALYSIS);
    assert(clamped.retention_days == IMAGE_PRIVACY_MAX_RETENTION_DAYS);
    assert(clamped.redaction_level == (uint8_t)IMAGE_REDACTION_STRICT);
    assert(image_privacy_is_valid(&clamped));
    assert(image_privacy_can_process(&clamped));
    assert(image_privacy_can_upload(&clamped));

    // Disabling clamps the derived upload and mode off even when set.
    image_privacy_policy_t disabled = clamped;
    disabled.enabled = 0u;
    disabled.upload_allowed = 1u;
    disabled.upload_scope = (uint8_t)IMAGE_UPLOAD_THUMBNAILS;
    disabled.processing_mode = (uint8_t)IMAGE_PROCESSING_LOCAL_FULL;
    const image_privacy_policy_t disabled_clamped =
        image_privacy_clamp(&disabled);
    assert(disabled_clamped.enabled == 0u);
    assert(disabled_clamped.upload_allowed == 0u);
    assert(disabled_clamped.upload_scope == (uint8_t)IMAGE_UPLOAD_NONE);
    assert(disabled_clamped.processing_mode == (uint8_t)IMAGE_PROCESSING_OFF);
    assert(!image_privacy_can_process(&disabled_clamped));
    assert(!image_privacy_can_upload(&disabled_clamped));

    // Local-only clamps upload off even when the caller requested it.
    image_privacy_policy_t local_only = clamped;
    local_only.local_processing_only = 1u;
    local_only.upload_allowed = 1u;
    local_only.upload_scope = (uint8_t)IMAGE_UPLOAD_ANALYSIS;
    const image_privacy_policy_t local_clamped =
        image_privacy_clamp(&local_only);
    assert(local_clamped.upload_allowed == 0u);
    assert(local_clamped.upload_scope == (uint8_t)IMAGE_UPLOAD_NONE);
    assert(!image_privacy_can_upload(&local_clamped));

    assert(!image_privacy_is_valid(nullptr));
    image_privacy_policy_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!image_privacy_is_valid(&wrong_version));
    assert(image_privacy_clamp(nullptr).processing_mode ==
           (uint8_t)IMAGE_PROCESSING_METADATA);

    // Catalog membership is bounded on both ends.
    assert(image_privacy_is_valid_mode(IMAGE_PROCESSING_OFF));
    assert(image_privacy_is_valid_mode(IMAGE_PROCESSING_LOCAL_FULL));
    assert(!image_privacy_is_valid_mode((image_processing_mode_t)99));
    assert(image_privacy_is_valid_scope(IMAGE_UPLOAD_NONE));
    assert(image_privacy_is_valid_scope(IMAGE_UPLOAD_ANALYSIS));
    assert(!image_privacy_is_valid_scope((image_upload_scope_t)99));
    assert(image_privacy_is_valid_redaction(IMAGE_REDACTION_NONE));
    assert(image_privacy_is_valid_redaction(IMAGE_REDACTION_STRICT));
    assert(!image_privacy_is_valid_redaction((image_redaction_level_t)99));
    assert(image_privacy_is_valid_block_reason(IMAGE_BLOCK_NONE));
    assert(image_privacy_is_valid_block_reason(IMAGE_BLOCK_ERROR));
    assert(!image_privacy_is_valid_block_reason((image_block_reason_t)99));

    // Stable names round-trip for the platform contract.
    assert(std::strcmp(image_privacy_mode_name(IMAGE_PROCESSING_OFF), "off")
           == 0);
    assert(std::strcmp(
               image_privacy_mode_name(IMAGE_PROCESSING_LOCAL_FULL),
               "local_full"
           ) == 0);
    assert(std::strcmp(
               image_privacy_scope_name(IMAGE_UPLOAD_THUMBNAILS),
               "thumbnails"
           ) == 0);
    assert(std::strcmp(
               image_privacy_redaction_name(IMAGE_REDACTION_STRICT),
               "strict"
           ) == 0);
    assert(std::strcmp(
               image_privacy_block_reason_name(IMAGE_BLOCK_DISABLED),
               "disabled"
           ) == 0);
    return 0;
}
