#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NETWORK_MANAGER_STATE_IDLE = 0,
    NETWORK_MANAGER_STATE_CONNECTING,
    NETWORK_MANAGER_STATE_CONNECTED,
} network_manager_state_t;

typedef void (*network_manager_state_callback_t)(
    network_manager_state_t state,
    void *context
);

network_manager_state_t network_manager_get_state(void);
esp_err_t network_manager_add_state_callback(
    network_manager_state_callback_t callback,
    void *context
);
esp_err_t network_manager_remove_state_callback(
    network_manager_state_callback_t callback,
    void *context
);

#ifdef __cplusplus
}
#endif
