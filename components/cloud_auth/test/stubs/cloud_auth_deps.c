#include "device_binding_client.h"
#include "network_manager.h"
#include "time_sync.h"

// Controllable host stand-ins for the four modules cloud_auth depends on. The
// test drives the device's network, clock, registration, and auth outcome
// directly so every state-machine transition can be exercised deterministically.
static network_manager_state_t host_network_state = NETWORK_MANAGER_STATE_IDLE;
static network_manager_state_callback_t host_network_callback;
static bool host_time_synchronized;
static bool host_registered;
static esp_err_t host_authenticate_result = ESP_OK;
static int host_authenticate_calls;
static int host_clear_session_calls;

network_manager_state_t network_manager_get_state(void) {
    return host_network_state;
}

esp_err_t network_manager_add_state_callback(
    network_manager_state_callback_t callback,
    void *context
) {
    (void)context;
    host_network_callback = callback;
    return ESP_OK;
}

bool time_sync_is_synchronized(void) {
    return host_time_synchronized;
}

bool device_binding_client_is_registered(void) {
    return host_registered;
}

esp_err_t device_binding_client_authenticate(void) {
    ++host_authenticate_calls;
    return host_authenticate_result;
}

esp_err_t device_binding_client_clear_session_token(void) {
    ++host_clear_session_calls;
    return ESP_OK;
}

// Test hooks.
void cloud_auth_host_set_network_state(network_manager_state_t state) {
    host_network_state = state;
}

void cloud_auth_host_emit_network_state(network_manager_state_t state) {
    host_network_state = state;
    if (host_network_callback != nullptr) {
        host_network_callback(state, nullptr);
    }
}

void cloud_auth_host_set_time_synchronized(bool synchronized) {
    host_time_synchronized = synchronized;
}

void cloud_auth_host_set_registered(bool registered) {
    host_registered = registered;
}

void cloud_auth_host_set_authenticate_result(esp_err_t result) {
    host_authenticate_result = result;
}

int cloud_auth_host_authenticate_calls(void) {
    return host_authenticate_calls;
}

int cloud_auth_host_clear_session_calls(void) {
    return host_clear_session_calls;
}
