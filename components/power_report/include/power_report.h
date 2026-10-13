#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report the live battery status to the platform once.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. The snapshot comes from `battery_power`, so
 * the reported charge state, percentage, voltage and power mode always match
 * the bounded state the device is actually applying.
 */
esp_err_t power_report_now(void);

/** @brief Return the removable-module descriptor for power reporting. */
const module_descriptor_t *power_report_module_descriptor(void);

#ifdef __cplusplus
}
#endif
