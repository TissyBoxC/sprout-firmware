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

ota_rollback_action_t ota_rollback_boot_action(
    bool pending_verify,
    bool healthy,
    uint32_t boot_count,
    uint32_t maximum_attempts
);

#ifdef __cplusplus
}
#endif
