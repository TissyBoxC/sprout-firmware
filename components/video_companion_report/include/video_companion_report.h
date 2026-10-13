#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report the live video-companionship status to the platform once.
 *
 * This closes the console-facing video-companion loop: the platform's
 * `video_companion` module exposes the device's latest call and media
 * permission state through `/api/v1/devices/{device_id}/video-companion/capability`,
 * but until a device reports it the row is never populated. The request uses
 * the device session token created by `device_binding_client`; a 401/403
 * clears the token so the next request re-authenticates instead of retrying a
 * revoked credential.
 */
esp_err_t video_companion_report_now(void);

/** @brief Return the removable-module descriptor for video-companion reporting. */
const module_descriptor_t *video_companion_report_module_descriptor(void);

#ifdef __cplusplus
}
#endif
