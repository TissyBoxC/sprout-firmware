#pragma once

typedef enum {
    NETWORK_MANAGER_STATE_IDLE = 0,
    NETWORK_MANAGER_STATE_CONNECTING,
    NETWORK_MANAGER_STATE_CONNECTED,
} network_manager_state_t;

network_manager_state_t network_manager_get_state(void);
