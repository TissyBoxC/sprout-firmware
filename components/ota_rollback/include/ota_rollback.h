#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize pending-verify boot accounting. */
esp_err_t ota_rollback_init(void);

/** @brief Return true when the running image is pending verification. */
bool ota_rollback_is_pending_verify(void);

/** @brief Return the persisted failed health-probe count. */
uint32_t ota_rollback_get_failed_probe_count(void);

/**
 * @brief Mark the running image valid and clear the boot attempt counter.
 *
 * Called only after the image has demonstrated a real health condition, such
 * as a successful authenticated platform request.
 */
esp_err_t ota_rollback_mark_valid(void);

/**
 * @brief Record a failed health probe and roll back when the budget is spent.
 *
 * Call this only after the platform is reachable and an authenticated
 * health probe fails. Boot attempts alone are not health failures: a slow
 * network must not discard an otherwise good image.
 */
esp_err_t ota_rollback_health_probe_failed(void);

/** @brief Return the removable-module descriptor for ota_rollback. */
const module_descriptor_t *ota_rollback_module_descriptor(void);

#ifdef __cplusplus
}
#endif
