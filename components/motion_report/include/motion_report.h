#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report the live motion controller state to the platform once.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. The lifecycle state, action and fault flag all
 * come from `motion_control`, so the guardian overview reflects the action the
 * body is actually running.
 */
esp_err_t motion_report_now(void);

/** @brief Return the removable-module descriptor for motion reporting. */
const module_descriptor_t *motion_report_module_descriptor(void);

#ifdef __cplusplus
}
#endif
