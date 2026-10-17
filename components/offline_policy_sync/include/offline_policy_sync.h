#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetch the fleet offline download policy and apply it locally.
 *
 * The request uses the device session token created by
 * `device_binding_client`; a 401/403 clears the token so the next request can
 * re-authenticate instead of retrying a revoked credential. The applied policy
 * is left untouched when the network, clock, or response payload is unusable.
 * An acknowledgement failure is returned after the policy is applied so the
 * worker retries without losing the successful local update.
 */
esp_err_t offline_policy_sync_now(void);

/** @brief Return the removable-module descriptor for offline policy sync. */
const module_descriptor_t *offline_policy_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
