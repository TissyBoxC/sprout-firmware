#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NETWORK_MANAGER_STATE_IDLE = 0,
    NETWORK_MANAGER_STATE_CONNECTED = 1,
} network_manager_state_t;

network_manager_state_t network_manager_get_state(void);

#ifdef __cplusplus
}
#endif
