#include "security_posture.h"

#include <stddef.h>

security_posture_verdict_t security_posture_evaluate(
    bool production_build,
    const security_posture_features_t *features
) {
    if (!production_build) {
        return SECURITY_POSTURE_VERDICT_OK;
    }
    if (features == NULL) {
        // A missing snapshot in a production build must never pass, so an
        // empty read is reported as the most fundamental missing control.
        return SECURITY_POSTURE_VERDICT_INSECURE_BOOT;
    }
    if (!features->secure_boot_enabled) {
        return SECURITY_POSTURE_VERDICT_INSECURE_BOOT;
    }
    if (!features->flash_encryption_enabled) {
        return SECURITY_POSTURE_VERDICT_UNENCRYPTED_FLASH;
    }
    return SECURITY_POSTURE_VERDICT_OK;
}
