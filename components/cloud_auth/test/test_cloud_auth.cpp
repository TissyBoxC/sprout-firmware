#include "cloud_auth.h"

#include "device_binding_client.h"
#include "network_manager.h"
#include "time_sync.h"

#include <cassert>
#include <cstring>

void cloud_auth_host_set_network_state(network_manager_state_t state);
void cloud_auth_host_emit_network_state(network_manager_state_t state);
void cloud_auth_host_set_time_synchronized(bool synchronized);
void cloud_auth_host_set_registered(bool registered);
void cloud_auth_host_set_authenticate_result(esp_err_t result);
int cloud_auth_host_authenticate_calls(void);
int cloud_auth_host_clear_session_calls(void);

namespace {

cloud_auth_state_t g_observed_state;
int g_callback_count;
int g_callback_context_marker;
void *g_observed_context;

void record_state(cloud_auth_state_t state, void *context) {
    g_observed_state = state;
    ++g_callback_count;
    g_observed_context = context;
}

void test_init_waits_for_network_when_offline() {
    cloud_auth_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    cloud_auth_host_set_registered(false);
    cloud_auth_host_set_time_synchronized(false);

    assert(cloud_auth_init() == ESP_OK);
    // Idempotent: a second init must not re-register or change state.
    assert(cloud_auth_init() == ESP_OK);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_WAITING_FOR_NETWORK);
    assert(!cloud_auth_is_ready());

    // A connection without a synchronized clock waits for time instead.
    cloud_auth_host_emit_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_WAITING_FOR_TIME);

    // A connection with a valid clock needs a fresh authentication.
    cloud_auth_host_set_time_synchronized(true);
    cloud_auth_host_emit_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_REAUTH_REQUIRED);

    // Dropping the link returns to the network-wait state.
    cloud_auth_host_emit_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_WAITING_FOR_NETWORK);
}

void test_ensure_authenticated_requires_preconditions() {
    // Offline: cannot authenticate.
    cloud_auth_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(cloud_auth_ensure_authenticated() == ESP_ERR_INVALID_STATE);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_WAITING_FOR_NETWORK);

    // Online but not registered: must go back to idle.
    cloud_auth_host_set_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    cloud_auth_host_set_registered(false);
    assert(cloud_auth_ensure_authenticated() == ESP_ERR_INVALID_STATE);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_IDLE);

    // Registered but the clock is not trusted yet.
    cloud_auth_host_set_registered(true);
    cloud_auth_host_set_time_synchronized(false);
    assert(cloud_auth_ensure_authenticated() == ESP_ERR_INVALID_STATE);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_WAITING_FOR_TIME);
}

void test_successful_authentication_becomes_ready() {
    cloud_auth_host_set_registered(true);
    cloud_auth_host_set_time_synchronized(true);
    cloud_auth_host_set_authenticate_result(ESP_OK);
    const int calls_before = cloud_auth_host_authenticate_calls();

    assert(cloud_auth_ensure_authenticated() == ESP_OK);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_READY);
    assert(cloud_auth_is_ready());
    assert(cloud_auth_host_authenticate_calls() == calls_before + 1);

    // Already ready: no extra platform round trip.
    assert(cloud_auth_ensure_authenticated() == ESP_OK);
    assert(cloud_auth_host_authenticate_calls() == calls_before + 1);
}

void test_failed_authentication_requires_reauth() {
    // Force the state back to re-auth, then fail the platform call.
    cloud_auth_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    cloud_auth_host_emit_network_state(NETWORK_MANAGER_STATE_IDLE);
    cloud_auth_host_set_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    cloud_auth_host_emit_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_REAUTH_REQUIRED);

    cloud_auth_host_set_authenticate_result(ESP_ERR_TIMEOUT);
    assert(cloud_auth_ensure_authenticated() == ESP_ERR_TIMEOUT);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_REAUTH_REQUIRED);
    assert(!cloud_auth_is_ready());
}

void test_unauthorized_clears_session_and_requires_reauth() {
    // Reach READY first so the unauthorized transition is a real state change.
    cloud_auth_host_set_authenticate_result(ESP_OK);
    assert(cloud_auth_ensure_authenticated() == ESP_OK);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_READY);

    // Register the observer before the transition so the callback and its
    // context are observed exactly once for this change.
    g_callback_count = 0;
    g_observed_state = CLOUD_AUTH_STATE_READY;
    g_observed_context = nullptr;
    assert(
        cloud_auth_set_state_callback(record_state, &g_callback_context_marker)
            == ESP_OK
    );

    const int clears_before = cloud_auth_host_clear_session_calls();
    assert(cloud_auth_mark_unauthorized() == ESP_OK);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_REAUTH_REQUIRED);
    assert(cloud_auth_host_clear_session_calls() == clears_before + 1);
    assert(!cloud_auth_is_ready());
    assert(g_callback_count == 1);
    assert(g_observed_state == CLOUD_AUTH_STATE_REAUTH_REQUIRED);
    assert(g_observed_context == &g_callback_context_marker);

    // Clearing the observer stops further notifications even though the
    // module keeps transitioning between READY and REAUTH_REQUIRED.
    assert(cloud_auth_set_state_callback(nullptr, nullptr) == ESP_OK);
    assert(cloud_auth_ensure_authenticated() == ESP_OK);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_READY);
    g_callback_count = 0;
    assert(cloud_auth_mark_unauthorized() == ESP_OK);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_REAUTH_REQUIRED);
    assert(g_callback_count == 0);

    // A 401 is transient, so a later attempt is allowed.
    cloud_auth_host_set_authenticate_result(ESP_OK);
    assert(cloud_auth_ensure_authenticated() == ESP_OK);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_READY);
}

void test_revoked_blocks_reauthentication() {
    const int clears_before = cloud_auth_host_clear_session_calls();
    assert(cloud_auth_mark_revoked() == ESP_OK);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_REVOKED);
    assert(cloud_auth_host_clear_session_calls() == clears_before + 1);

    // A platform 403 is terminal until re-provisioning: re-auth is refused
    // even though the network and clock are fine.
    const int auth_calls_before = cloud_auth_host_authenticate_calls();
    assert(cloud_auth_ensure_authenticated() == ESP_ERR_INVALID_STATE);
    assert(cloud_auth_get_state() == CLOUD_AUTH_STATE_REVOKED);
    assert(cloud_auth_host_authenticate_calls() == auth_calls_before);
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor = cloud_auth_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "cloud_auth") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

}  // namespace

int main() {
    test_init_waits_for_network_when_offline();
    test_ensure_authenticated_requires_preconditions();
    test_successful_authentication_becomes_ready();
    test_failed_authentication_requires_reauth();
    test_unauthorized_clears_session_and_requires_reauth();
    test_revoked_blocks_reauthentication();
    test_module_descriptor_is_stable();
    return 0;
}
