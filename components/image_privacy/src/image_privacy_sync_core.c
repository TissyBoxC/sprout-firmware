#include "image_privacy_sync_core.h"

#include <string.h>

bool image_privacy_sync_core_parse_mode(
    const char *value,
    image_processing_mode_t *mode_out
) {
    if (value == NULL || mode_out == NULL) {
        return false;
    }
    if (strcmp(value, "off") == 0) {
        *mode_out = IMAGE_PROCESSING_OFF;
        return true;
    }
    if (strcmp(value, "metadata") == 0) {
        *mode_out = IMAGE_PROCESSING_METADATA;
        return true;
    }
    if (strcmp(value, "local_full") == 0) {
        *mode_out = IMAGE_PROCESSING_LOCAL_FULL;
        return true;
    }
    return false;
}

bool image_privacy_sync_core_parse_scope(
    const char *value,
    image_upload_scope_t *scope_out
) {
    if (value == NULL || scope_out == NULL) {
        return false;
    }
    if (strcmp(value, "none") == 0) {
        *scope_out = IMAGE_UPLOAD_NONE;
        return true;
    }
    if (strcmp(value, "thumbnails") == 0) {
        *scope_out = IMAGE_UPLOAD_THUMBNAILS;
        return true;
    }
    if (strcmp(value, "analysis") == 0) {
        *scope_out = IMAGE_UPLOAD_ANALYSIS;
        return true;
    }
    return false;
}

bool image_privacy_sync_core_parse_redaction(
    const char *value,
    image_redaction_level_t *level_out
) {
    if (value == NULL || level_out == NULL) {
        return false;
    }
    if (strcmp(value, "none") == 0) {
        *level_out = IMAGE_REDACTION_NONE;
        return true;
    }
    if (strcmp(value, "standard") == 0) {
        *level_out = IMAGE_REDACTION_STANDARD;
        return true;
    }
    if (strcmp(value, "strict") == 0) {
        *level_out = IMAGE_REDACTION_STRICT;
        return true;
    }
    return false;
}

bool image_privacy_sync_core_build_policy(
    const image_privacy_remote_t *remote,
    image_privacy_policy_t *policy_out
) {
    if (remote == NULL || policy_out == NULL) {
        return false;
    }
    image_processing_mode_t mode = IMAGE_PROCESSING_OFF;
    if (!image_privacy_sync_core_parse_mode(remote->processing_mode, &mode)) {
        return false;
    }
    image_upload_scope_t scope = IMAGE_UPLOAD_NONE;
    if (!image_privacy_sync_core_parse_scope(remote->upload_scope, &scope)) {
        return false;
    }
    image_redaction_level_t redaction = IMAGE_REDACTION_STRICT;
    if (!image_privacy_sync_core_parse_redaction(
            remote->redaction_level,
            &redaction
        )) {
        return false;
    }
    uint32_t retention_days = remote->retention_days < 0
        ? 0u
        : (uint32_t)remote->retention_days;
    image_privacy_policy_t policy = image_privacy_default();
    policy.enabled = remote->enabled ? 1u : 0u;
    policy.upload_allowed = remote->upload_allowed ? 1u : 0u;
    policy.local_processing_only = remote->local_processing_only ? 1u : 0u;
    policy.processing_mode = (uint8_t)mode;
    policy.upload_scope = (uint8_t)scope;
    policy.retention_days = (uint8_t)retention_days;
    policy.redaction_level = (uint8_t)redaction;
    policy.require_child_consent = remote->require_child_consent ? 1u : 0u;
    // The clamp pins the mode/scope off when the feature is disabled and
    // revokes upload whenever it is unauthorised or local-only.
    *policy_out = image_privacy_clamp(&policy);
    return true;
}
