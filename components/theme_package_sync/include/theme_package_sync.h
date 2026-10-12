#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetch the published theme packs and atomically replace the cache.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. The local cache is left untouched when the
 * network, clock, or response payload is unusable.
 */
esp_err_t theme_package_sync_now(void);

/** @brief Return the removable-module descriptor for theme pack sync. */
const module_descriptor_t *theme_package_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
