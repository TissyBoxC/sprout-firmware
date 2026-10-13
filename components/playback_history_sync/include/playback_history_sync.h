#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report the locally recorded playback outcomes to the platform.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. Each accepted record is dropped from the
 * pending queue, while a transient failure leaves the rest for the next cycle.
 */
esp_err_t playback_history_sync_now(void);

/** @brief Return the removable-module descriptor for playback history sync. */
const module_descriptor_t *playback_history_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
