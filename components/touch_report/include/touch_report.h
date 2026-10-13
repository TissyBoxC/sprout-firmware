#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report the live touch configuration to the platform once.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. The capability flags and the enabled gesture
 * count come from `touch_input`, so the guardian overview reflects the gestures
 * the surface will actually recognise.
 */
esp_err_t touch_report_now(void);

/** @brief Return the removable-module descriptor for touch reporting. */
const module_descriptor_t *touch_report_module_descriptor(void);

#ifdef __cplusplus
}
#endif
