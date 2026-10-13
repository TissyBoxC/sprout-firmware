#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetch the family alarm schedule and replace the local cache.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. The local firing state of surviving alarms is
 * preserved, and the cache is left untouched when the network, clock, or
 * response payload is unusable.
 */
esp_err_t alarm_reminder_sync_now(void);

/** @brief Return the removable-module descriptor for alarm schedule sync. */
const module_descriptor_t *alarm_reminder_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
