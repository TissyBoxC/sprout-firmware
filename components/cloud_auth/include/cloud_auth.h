#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Cloud authentication lifecycle states. */
typedef enum {
    CLOUD_AUTH_STATE_IDLE = 0,
    CLOUD_AUTH_STATE_WAITING_FOR_NETWORK,
    CLOUD_AUTH_STATE_WAITING_FOR_TIME,
    CLOUD_AUTH_STATE_AUTHENTICATING,
    CLOUD_AUTH_STATE_READY,
    CLOUD_AUTH_STATE_REAUTH_REQUIRED,
} cloud_auth_state_t;

/** @brief Initialize session lifecycle tracking. */
esp_err_t cloud_auth_init(void);

/** @brief Return the current authentication state. */
cloud_auth_state_t cloud_auth_get_state(void);

/** @brief Return true when the device has a usable short-lived session. */
bool cloud_auth_is_ready(void);

/** @brief Authenticate if the clock and network permit it. */
esp_err_t cloud_auth_ensure_authenticated(void);

/** @brief Clear the local session after a platform authorization rejection. */
esp_err_t cloud_auth_mark_unauthorized(void);

/** @brief Return the removable-module descriptor for cloud_auth. */
const module_descriptor_t *cloud_auth_module_descriptor(void);

#ifdef __cplusplus
}
#endif
