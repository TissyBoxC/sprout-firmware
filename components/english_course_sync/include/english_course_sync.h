#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetch the published English lessons and replace the lesson cache.
 *
 * The request uses the device session token created by `device_binding_client`;
 * a 401/403 clears the token so the next request can re-authenticate instead of
 * retrying a revoked credential. The cached lessons are left untouched when the
 * network, clock, or response payload is unusable, and the local practice log
 * is preserved across every refresh.
 *
 * After a successful refresh the queued practice attempts are reported to the
 * platform as well.
 */
esp_err_t english_course_sync_now(void);

/** @brief Return the removable-module descriptor for English lesson sync. */
const module_descriptor_t *english_course_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
