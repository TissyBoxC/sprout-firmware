#include "image_privacy.h"

#include <stddef.h>

static uint8_t clamp_u8(uint32_t value, uint32_t minimum, uint32_t maximum) {
    if (value < minimum) {
        return (uint8_t)minimum;
    }
    if (value > maximum) {
        return (uint8_t)maximum;
    }
    return (uint8_t)value;
}

image_privacy_policy_t image_privacy_default(void) {
    const image_privacy_policy_t policy = {
        .schema_version = IMAGE_PRIVACY_SCHEMA_VERSION,
        // Processing and upload start off, local-only processing starts on, so
        // the feature is opt-in with the safest possible defaults.
        .enabled = 0u,
        .upload_allowed = 0u,
        .local_processing_only = 1u,
        .processing_mode = (uint8_t)IMAGE_PROCESSING_METADATA,
        .upload_scope = (uint8_t)IMAGE_UPLOAD_NONE,
        .retention_days = 0u,
        .redaction_level = (uint8_t)IMAGE_REDACTION_STRICT,
        .require_child_consent = 1u,
        .reserved = 0u,
    };
    return policy;
}

image_privacy_policy_t image_privacy_clamp(
    const image_privacy_policy_t *policy
) {
    image_privacy_policy_t clamped = image_privacy_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.schema_version = IMAGE_PRIVACY_SCHEMA_VERSION;
    clamped.enabled = policy->enabled != 0u ? 1u : 0u;
    clamped.upload_allowed = policy->upload_allowed != 0u ? 1u : 0u;
    clamped.local_processing_only =
        policy->local_processing_only != 0u ? 1u : 0u;
    clamped.processing_mode = clamp_u8(
        policy->processing_mode,
        (uint32_t)IMAGE_PROCESSING_OFF,
        (uint32_t)IMAGE_PROCESSING_LOCAL_FULL
    );
    clamped.upload_scope = clamp_u8(
        policy->upload_scope,
        (uint32_t)IMAGE_UPLOAD_NONE,
        (uint32_t)IMAGE_UPLOAD_ANALYSIS
    );
    clamped.retention_days = clamp_u8(
        policy->retention_days,
        IMAGE_PRIVACY_MIN_RETENTION_DAYS,
        IMAGE_PRIVACY_MAX_RETENTION_DAYS
    );
    clamped.redaction_level = clamp_u8(
        policy->redaction_level,
        (uint32_t)IMAGE_REDACTION_NONE,
        (uint32_t)IMAGE_REDACTION_STRICT
    );
    clamped.require_child_consent =
        policy->require_child_consent != 0u ? 1u : 0u;
    clamped.reserved = 0u;
    // A disabled feature cannot process; the derived mode is pinned off here
    // so a stale processing setting never survives the switch.
    if (clamped.enabled == 0u) {
        clamped.upload_allowed = 0u;
        clamped.upload_scope = (uint8_t)IMAGE_UPLOAD_NONE;
        clamped.processing_mode = (uint8_t)IMAGE_PROCESSING_OFF;
    }
    // Upload is a derived permission: an unauthorised upload always has the
    // empty scope, and local-only processing never coexists with an upload.
    if (clamped.upload_allowed == 0u || clamped.local_processing_only != 0u) {
        clamped.upload_allowed = 0u;
        clamped.upload_scope = (uint8_t)IMAGE_UPLOAD_NONE;
    }
    return clamped;
}

bool image_privacy_is_valid(const image_privacy_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != IMAGE_PRIVACY_SCHEMA_VERSION) {
        return false;
    }
    if (policy->processing_mode > (uint8_t)IMAGE_PROCESSING_LOCAL_FULL) {
        return false;
    }
    if (policy->upload_scope > (uint8_t)IMAGE_UPLOAD_ANALYSIS) {
        return false;
    }
    if (policy->retention_days > IMAGE_PRIVACY_MAX_RETENTION_DAYS) {
        return false;
    }
    if (policy->redaction_level > (uint8_t)IMAGE_REDACTION_STRICT) {
        return false;
    }
    return true;
}

bool image_privacy_can_process(const image_privacy_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    const image_privacy_policy_t bounded = image_privacy_clamp(policy);
    return bounded.enabled != 0u &&
           bounded.processing_mode != (uint8_t)IMAGE_PROCESSING_OFF;
}

bool image_privacy_can_upload(const image_privacy_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    const image_privacy_policy_t bounded = image_privacy_clamp(policy);
    return image_privacy_can_process(&bounded) &&
           bounded.upload_allowed != 0u &&
           bounded.upload_scope != (uint8_t)IMAGE_UPLOAD_NONE;
}

bool image_privacy_is_valid_mode(image_processing_mode_t mode) {
    return mode >= IMAGE_PROCESSING_OFF && mode <= IMAGE_PROCESSING_LOCAL_FULL;
}

bool image_privacy_is_valid_scope(image_upload_scope_t scope) {
    return scope >= IMAGE_UPLOAD_NONE && scope <= IMAGE_UPLOAD_ANALYSIS;
}

bool image_privacy_is_valid_redaction(image_redaction_level_t level) {
    return level >= IMAGE_REDACTION_NONE && level <= IMAGE_REDACTION_STRICT;
}

bool image_privacy_is_valid_block_reason(image_block_reason_t reason) {
    return reason >= IMAGE_BLOCK_NONE && reason <= IMAGE_BLOCK_ERROR;
}

const char *image_privacy_error_name(image_privacy_error_t error) {
    switch (error) {
        case IMAGE_PRIVACY_OK:
            return "ok";
        case IMAGE_PRIVACY_ERR_NULL_ARGUMENT:
            return "null_argument";
        case IMAGE_PRIVACY_ERR_INVALID_POLICY:
            return "invalid_policy";
        case IMAGE_PRIVACY_ERR_INVALID_STATE:
            return "invalid_state";
        case IMAGE_PRIVACY_ERR_PERMISSION_DENIED:
            return "permission_denied";
        case IMAGE_PRIVACY_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

const char *image_privacy_mode_name(image_processing_mode_t mode) {
    switch (mode) {
        case IMAGE_PROCESSING_OFF:
            return "off";
        case IMAGE_PROCESSING_METADATA:
            return "metadata";
        case IMAGE_PROCESSING_LOCAL_FULL:
            return "local_full";
        default:
            return "unknown";
    }
}

const char *image_privacy_scope_name(image_upload_scope_t scope) {
    switch (scope) {
        case IMAGE_UPLOAD_NONE:
            return "none";
        case IMAGE_UPLOAD_THUMBNAILS:
            return "thumbnails";
        case IMAGE_UPLOAD_ANALYSIS:
            return "analysis";
        default:
            return "unknown";
    }
}

const char *image_privacy_redaction_name(image_redaction_level_t level) {
    switch (level) {
        case IMAGE_REDACTION_NONE:
            return "none";
        case IMAGE_REDACTION_STANDARD:
            return "standard";
        case IMAGE_REDACTION_STRICT:
            return "strict";
        default:
            return "unknown";
    }
}

const char *image_privacy_block_reason_name(image_block_reason_t reason) {
    switch (reason) {
        case IMAGE_BLOCK_NONE:
            return "none";
        case IMAGE_BLOCK_DISABLED:
            return "disabled";
        case IMAGE_BLOCK_CONSENT:
            return "consent";
        case IMAGE_BLOCK_SCOPE:
            return "scope";
        case IMAGE_BLOCK_QUOTA:
            return "quota";
        case IMAGE_BLOCK_ERROR:
            return "error";
        default:
            return "unknown";
    }
}
