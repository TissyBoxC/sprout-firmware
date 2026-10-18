#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_ROLLBACK_ACTION_NONE = 0,
    OTA_ROLLBACK_ACTION_MARK_VALID,
    OTA_ROLLBACK_ACTION_REBOOT_INVALID,
} ota_rollback_action_t;

bool ota_rollback_version_allowed(
    const char *current_version,
    const char *candidate_version,
    bool rollback_allowed,
    bool current_image_invalid
);

bool ota_rollback_boot_count_exceeded(
    uint32_t boot_count,
    uint32_t maximum_attempts
);

ota_rollback_action_t ota_rollback_health_action(
    bool pending_verify,
    bool health_probe_succeeded,
    uint32_t failed_probe_count,
    uint32_t maximum_attempts
);

/**
 * Decide whether a failed platform probe is authoritative enough to consume
 * the rollback budget. Transport failures and transient service responses do
 * not; authentication, contract, and permanent request failures do.
 */
bool ota_rollback_health_probe_failure_is_definitive(
    bool request_completed,
    int http_status_code,
    bool response_invalid
);

#ifdef __cplusplus
}
#endif
