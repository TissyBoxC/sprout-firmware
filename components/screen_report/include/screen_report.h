#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report the live screen surface state to the platform once.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. The capability flags and the active
 * expression, animation and video state all come from `screen_surface`, so the
 * guardian overview reflects what the surface is actually rendering.
 */
esp_err_t screen_report_now(void);

/** @brief Return the removable-module descriptor for screen reporting. */
const module_descriptor_t *screen_report_module_descriptor(void);

#ifdef __cplusplus
}
#endif
