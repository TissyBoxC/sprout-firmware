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

/** @brief Return the current pending-verify boot attempt count. */
uint32_t ota_rollback_get_boot_count(void);

/**
 * @brief Mark the running image valid and clear the boot attempt counter.
 *
 * Called only after the image has demonstrated a real health condition, such
 * as a successful authenticated platform request.
 */
esp_err_t ota_rollback_mark_valid(void);

/**
 * @brief Roll back and reboot when the pending image is not healthy.
 *
 * The function only acts after the configured boot-attempt budget is
 * exhausted, so a slow network does not discard an otherwise good image.
 */
esp_err_t ota_rollback_reboot_if_unhealthy(void);

/** @brief Return the removable-module descriptor for ota_rollback. */
const module_descriptor_t *ota_rollback_module_descriptor(void);

#ifdef __cplusplus
}
#endif
