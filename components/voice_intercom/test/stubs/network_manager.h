#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NETWORK_MANAGER_STATE_DISCONNECTED = 0,
    NETWORK_MANAGER_STATE_CONNECTING,
    NETWORK_MANAGER_STATE_CONNECTED,
} network_manager_state_t;

network_manager_state_t network_manager_get_state(void);

#ifdef __cplusplus
}
#endif
