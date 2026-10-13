#include "image_privacy_sync_core.h"

#include <cassert>
#include <cstring>

int main() {
    // Every catalog processing mode maps to its enum; an unknown is rejected.
    image_processing_mode_t mode = IMAGE_PROCESSING_OFF;
    assert(image_privacy_sync_core_parse_mode("off", &mode));
    assert(mode == IMAGE_PROCESSING_OFF);
    assert(image_privacy_sync_core_parse_mode("metadata", &mode));
    assert(mode == IMAGE_PROCESSING_METADATA);
    assert(image_privacy_sync_core_parse_mode("local_full", &mode));
    assert(mode == IMAGE_PROCESSING_LOCAL_FULL);
    assert(!image_privacy_sync_core_parse_mode("cloud", &mode));
    assert(!image_privacy_sync_core_parse_mode(nullptr, &mode));

    // Every catalog upload scope maps to its enum; an unknown is rejected.
    image_upload_scope_t scope = IMAGE_UPLOAD_NONE;
    assert(image_privacy_sync_core_parse_scope("none", &scope));
    assert(scope == IMAGE_UPLOAD_NONE);
    assert(image_privacy_sync_core_parse_scope("thumbnails", &scope));
    assert(scope == IMAGE_UPLOAD_THUMBNAILS);
    assert(image_privacy_sync_core_parse_scope("analysis", &scope));
    assert(scope == IMAGE_UPLOAD_ANALYSIS);
    assert(!image_privacy_sync_core_parse_scope("raw", &scope));

    // Every catalog redaction level maps to its enum; an unknown is rejected.
    image_redaction_level_t redaction = IMAGE_REDACTION_NONE;
    assert(image_privacy_sync_core_parse_redaction("none", &redaction));
    assert(redaction == IMAGE_REDACTION_NONE);
    assert(image_privacy_sync_core_parse_redaction("standard", &redaction));
    assert(redaction == IMAGE_REDACTION_STANDARD);
    assert(image_privacy_sync_core_parse_redaction("strict", &redaction));
    assert(redaction == IMAGE_REDACTION_STRICT);
    assert(!image_privacy_sync_core_parse_redaction("blur", &redaction));

    // A fully populated enabled policy is clamped and mapped.
    image_privacy_remote_t remote = {};
    remote.enabled = true;
    remote.upload_allowed = true;
    remote.local_processing_only = false;
    remote.processing_mode = "local_full";
    remote.upload_scope = "analysis";
    remote.retention_days = 999;
    remote.redaction_level = "standard";
    remote.require_child_consent = false;
    remote.revision = 3;
    image_privacy_policy_t policy = {};
    assert(image_privacy_sync_core_build_policy(&remote, &policy));
    assert(policy.schema_version == IMAGE_PRIVACY_SCHEMA_VERSION);
    assert(policy.enabled == 1u);
    assert(policy.upload_allowed == 1u);
    assert(policy.local_processing_only == 0u);
    assert(policy.processing_mode == (uint8_t)IMAGE_PROCESSING_LOCAL_FULL);
    assert(policy.upload_scope == (uint8_t)IMAGE_UPLOAD_ANALYSIS);
    assert(policy.retention_days == IMAGE_PRIVACY_MAX_RETENTION_DAYS);
    assert(policy.redaction_level == (uint8_t)IMAGE_REDACTION_STANDARD);
    assert(policy.require_child_consent == 0u);
    assert(policy.revision == 3u);
    assert(image_privacy_is_valid(&policy));

    // A disabled policy pins the processing mode off and revokes upload.
    image_privacy_remote_t disabled = remote;
    disabled.enabled = false;
    disabled.upload_allowed = true;
    disabled.processing_mode = "local_full";
    image_privacy_policy_t disabled_policy = {};
    assert(image_privacy_sync_core_build_policy(&disabled, &disabled_policy));
    assert(disabled_policy.enabled == 0u);
    assert(disabled_policy.processing_mode == (uint8_t)IMAGE_PROCESSING_OFF);
    assert(disabled_policy.upload_allowed == 0u);
    assert(disabled_policy.upload_scope == (uint8_t)IMAGE_UPLOAD_NONE);
    assert(!image_privacy_can_process(&disabled_policy));
    assert(!image_privacy_can_upload(&disabled_policy));

    // Local-only processing revokes upload even when it is authorised.
    image_privacy_remote_t local_only = remote;
    local_only.local_processing_only = true;
    local_only.upload_allowed = true;
    local_only.upload_scope = "thumbnails";
    image_privacy_policy_t local_only_policy = {};
    assert(image_privacy_sync_core_build_policy(&local_only, &local_only_policy));
    assert(image_privacy_can_process(&local_only_policy));
    assert(local_only_policy.upload_allowed == 0u);
    assert(local_only_policy.upload_scope == (uint8_t)IMAGE_UPLOAD_NONE);
    assert(!image_privacy_can_upload(&local_only_policy));

    // A negative retention window floors at the minimum.
    image_privacy_remote_t negative = remote;
    negative.retention_days = -5;
    image_privacy_policy_t negative_policy = {};
    assert(image_privacy_sync_core_build_policy(&negative, &negative_policy));
    assert(negative_policy.retention_days == IMAGE_PRIVACY_MIN_RETENTION_DAYS);

    // An unknown mode, scope or redaction rejects the whole policy.
    image_privacy_remote_t bad_mode = remote;
    bad_mode.processing_mode = "cloud";
    image_privacy_policy_t bad_mode_policy = {};
    assert(!image_privacy_sync_core_build_policy(&bad_mode, &bad_mode_policy));
    image_privacy_remote_t bad_scope = remote;
    bad_scope.upload_scope = "raw";
    image_privacy_policy_t bad_scope_policy = {};
    assert(!image_privacy_sync_core_build_policy(&bad_scope, &bad_scope_policy));
    image_privacy_remote_t bad_redaction = remote;
    bad_redaction.redaction_level = "blur";
    image_privacy_policy_t bad_redaction_policy = {};
    assert(!image_privacy_sync_core_build_policy(
        &bad_redaction,
        &bad_redaction_policy
    ));

    return 0;
}
