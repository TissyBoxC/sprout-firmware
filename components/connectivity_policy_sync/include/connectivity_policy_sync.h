#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetch the family connectivity policy and apply it locally once.
 *
 * The request uses the device session token created by
 * `device_binding_client`; a 401/403 clears the token so the next request can
 * re-authenticate instead of retrying a revoked credential. The applied policy
 * is left untouched when the network, clock, or response payload is unusable.
 */
esp_err_t connectivity_policy_sync_now(void);

/**
 * @brief Return the revision of the connectivity policy last applied.
 *
 * Zero means the device has not applied a guardian connectivity policy yet, so
 * a reporter can send the truthful applied revision instead of a hardcoded
 * value. The revision survives a reboot through the configuration store.
 */
uint32_t connectivity_policy_applied_revision(void);

/** @brief Return the removable-module descriptor for connectivity sync. */
const module_descriptor_t *connectivity_policy_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
