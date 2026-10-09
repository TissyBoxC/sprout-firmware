#include "ota_rollback_policy.h"

#include "ota_manifest.h"

bool ota_rollback_version_allowed(
    const char *current_version,
    const char *candidate_version,
    bool rollback_allowed,
    bool current_image_invalid
) {
    if (current_version == NULL || candidate_version == NULL) {
        return false;
    }
    const int comparison = ota_version_compare(
        candidate_version,
        current_version
    );
    if (comparison > 0) {
        return true;
    }
    return comparison < 0 && rollback_allowed && current_image_invalid;
}

bool ota_rollback_boot_count_exceeded(
    uint32_t boot_count,
    uint32_t maximum_attempts
) {
    return maximum_attempts > 0 && boot_count >= maximum_attempts;
}

ota_rollback_action_t ota_rollback_boot_action(
    bool pending_verify,
    bool healthy,
    uint32_t boot_count,
    uint32_t maximum_attempts
) {
    if (!pending_verify) {
        return OTA_ROLLBACK_ACTION_NONE;
    }
    if (healthy) {
        return OTA_ROLLBACK_ACTION_MARK_VALID;
    }
    return ota_rollback_boot_count_exceeded(boot_count, maximum_attempts)
               ? OTA_ROLLBACK_ACTION_REBOOT_INVALID
               : OTA_ROLLBACK_ACTION_NONE;
}
