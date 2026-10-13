#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report the live network bearer and signal to the platform once.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. The device has no cellular modem in this
 * image, so it reports the Wi-Fi bearer and the measured signal percentage; the
 * platform keeps the latest bounded status for the guardian overview.
 */
esp_err_t connectivity_report_now(void);

/** @brief Return the removable-module descriptor for connectivity reporting. */
const module_descriptor_t *connectivity_report_module_descriptor(void);

#ifdef __cplusplus
}
#endif
