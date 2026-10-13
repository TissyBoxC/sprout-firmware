#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report the composed form-factor profile to the platform once.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. The platform records the technical name and
 * the capability mask and derives the served action set, so the guardian view
 * always matches the hardware actually present.
 */
esp_err_t form_factor_report_now(void);

/** @brief Return the removable-module descriptor for the form-factor report. */
const module_descriptor_t *form_factor_report_module_descriptor(void);

#ifdef __cplusplus
}
#endif
