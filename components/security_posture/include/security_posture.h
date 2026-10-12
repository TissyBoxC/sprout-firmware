#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Hardware attestation read from the device at boot. */
typedef struct {
    bool secure_boot_enabled;
    bool flash_encryption_enabled;
} security_posture_features_t;

/** @brief Pure verdict for one build profile and one hardware snapshot. */
typedef enum {
    SECURITY_POSTURE_VERDICT_OK = 0,
    SECURITY_POSTURE_VERDICT_INSECURE_BOOT,
    SECURITY_POSTURE_VERDICT_UNENCRYPTED_FLASH,
} security_posture_verdict_t;

/**
 * @brief Decide whether a build may run given its hardware controls.
 *
 * Development and factory images accept any posture so provisioning can
 * happen on the bench. A production image must have both Secure Boot and
 * Flash Encryption active; the first missing control is reported so the
 * operator knows what to fix. A NULL snapshot in a production build is
 * treated as unhardened rather than as an implicit pass.
 */
security_posture_verdict_t security_posture_evaluate(
    bool production_build,
    const security_posture_features_t *features
);

/** @brief Read the live Secure Boot and Flash Encryption state. */
security_posture_features_t security_posture_read_features(void);

/**
 * @brief Enforce the build's required posture.
 *
 * Returns ESP_OK when the running image satisfies its contract, otherwise
 * ESP_ERR_INVALID_STATE with a stable log line naming the missing control.
 */
esp_err_t security_posture_enforce(void);

/**
 * @brief Halt an image that failed enforcement.
 *
 * Never returns. The device keeps resetting and never reaches module or
 * network startup, so an unhardened production image cannot expose device
 * secrets or child data.
 */
void security_posture_halt(void);

/**
 * @brief Enforce the posture and halt on failure.
 *
 * Returns ESP_OK only when the image satisfies its contract; otherwise it
 * calls security_posture_halt() and never returns. This is the entry point
 * wired into the first module slot, so it runs before any other feature.
 */
esp_err_t security_posture_guard(void);

/** @brief Return the removable-module descriptor for security_posture. */
const module_descriptor_t *security_posture_module_descriptor(void);

#ifdef __cplusplus
}
#endif
