#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pull published device_firmware UI text and apply it locally.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. The device holds exactly one active package,
 * so the newest eligible publication wins and a withdrawal reverts to built-in
 * text. The local package is left untouched when the network, clock, or
 * response payload is unusable.
 */
esp_err_t ui_text_sync_now(void);

/** @brief Return the removable-module descriptor for UI text sync. */
const module_descriptor_t *ui_text_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
