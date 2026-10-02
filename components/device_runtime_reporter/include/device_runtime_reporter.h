#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize periodic runtime reporting.
 *
 * Requires provisioning, cloud authentication, and runtime state modules.
 * The worker waits for a trusted clock and an authenticated session before
 * sending the first heartbeat.
 */
esp_err_t device_runtime_reporter_init(void);

/** @brief Return the removable-module descriptor for device_runtime_reporter. */
const module_descriptor_t *device_runtime_reporter_module_descriptor(void);

#ifdef __cplusplus
}
#endif
