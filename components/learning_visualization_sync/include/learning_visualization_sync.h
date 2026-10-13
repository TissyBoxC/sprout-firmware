#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetch the family learning-visualization policy and apply it locally.
 *
 * The request uses the device session token created by
 * `device_binding_client`; a 401/403 clears the token so the next request can
 * re-authenticate instead of retrying a revoked credential. The applied policy
 * is left untouched when the network, clock, or response payload is unusable.
 */
esp_err_t learning_visualization_sync_now(void);

/** @brief Return the removable-module descriptor for learning visualization sync. */
const module_descriptor_t *learning_visualization_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
