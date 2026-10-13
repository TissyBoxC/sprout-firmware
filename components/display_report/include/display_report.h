#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report the live display status to the platform once.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. Brightness comes from the applied eye-care
 * policy, the panel size from the build configuration and touch support from
 * the compiled capability set, so the guardian display view matches the panel
 * actually present.
 */
esp_err_t display_report_now(void);

/** @brief Return the removable-module descriptor for display reporting. */
const module_descriptor_t *display_report_module_descriptor(void);

#ifdef __cplusplus
}
#endif
