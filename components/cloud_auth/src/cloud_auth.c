#include "cloud_auth.h"

#include "device_binding_client.h"
#include "esp_log.h"
#include "network_manager.h"
#include "time_sync.h"

static const char *const TAG = "cloud_auth";

static bool cloud_auth_ready;
static cloud_auth_state_t cloud_auth_state = CLOUD_AUTH_STATE_IDLE;
static cloud_auth_state_callback_t cloud_auth_callback;
static void *cloud_auth_callback_context;

static void cloud_auth_set_state(cloud_auth_state_t state) {
    if (cloud_auth_state == state) {
        return;
    }
    cloud_auth_state = state;
    if (cloud_auth_callback != NULL) {
        cloud_auth_callback(state, cloud_auth_callback_context);
    }
}

static void cloud_auth_network_callback(
    network_manager_state_t state,
    void *context
) {
    (void)context;
    if (state != NETWORK_MANAGER_STATE_CONNECTED) {
        cloud_auth_set_state(CLOUD_AUTH_STATE_WAITING_FOR_NETWORK);
        return;
    }
    if (time_sync_is_synchronized()) {
        cloud_auth_set_state(CLOUD_AUTH_STATE_REAUTH_REQUIRED);
    } else {
        cloud_auth_set_state(CLOUD_AUTH_STATE_WAITING_FOR_TIME);
    }
}

esp_err_t cloud_auth_init(void) {
    if (cloud_auth_ready) {
        return ESP_OK;
    }
    const esp_err_t callback_result = network_manager_add_state_callback(
        cloud_auth_network_callback,
        NULL
    );
    if (callback_result != ESP_OK) {
        return callback_result;
    }
    cloud_auth_ready = true;
    cloud_auth_set_state(
        network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED
            ? CLOUD_AUTH_STATE_REAUTH_REQUIRED
            : CLOUD_AUTH_STATE_WAITING_FOR_NETWORK
    );
    return ESP_OK;
}

cloud_auth_state_t cloud_auth_get_state(void) {
    return cloud_auth_state;
}

bool cloud_auth_is_ready(void) {
    return cloud_auth_state == CLOUD_AUTH_STATE_READY;
}

esp_err_t cloud_auth_ensure_authenticated(void) {
    if (!cloud_auth_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (cloud_auth_state == CLOUD_AUTH_STATE_REVOKED) {
        // A platform 403 is not a transient missing token. Re-authenticating
        // immediately would recreate the session the platform just revoked.
        return ESP_ERR_INVALID_STATE;
    }
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        cloud_auth_set_state(CLOUD_AUTH_STATE_WAITING_FOR_NETWORK);
        return ESP_ERR_INVALID_STATE;
    }
    if (!device_binding_client_is_registered()) {
        cloud_auth_set_state(CLOUD_AUTH_STATE_IDLE);
        return ESP_ERR_INVALID_STATE;
    }
    if (!time_sync_is_synchronized()) {
        cloud_auth_set_state(CLOUD_AUTH_STATE_WAITING_FOR_TIME);
        return ESP_ERR_INVALID_STATE;
    }
    if (cloud_auth_state == CLOUD_AUTH_STATE_READY) {
        return ESP_OK;
    }
    cloud_auth_set_state(CLOUD_AUTH_STATE_AUTHENTICATING);
    const esp_err_t result = device_binding_client_authenticate();
    if (result != ESP_OK) {
        cloud_auth_set_state(CLOUD_AUTH_STATE_REAUTH_REQUIRED);
        ESP_LOGW(
            TAG,
            "device authentication failed: %s",
            esp_err_to_name(result)
        );
        return result;
    }
    cloud_auth_set_state(CLOUD_AUTH_STATE_READY);
    return ESP_OK;
}

esp_err_t cloud_auth_mark_unauthorized(void) {
    const esp_err_t result = device_binding_client_clear_session_token();
    cloud_auth_set_state(CLOUD_AUTH_STATE_REAUTH_REQUIRED);
    return result;
}

esp_err_t cloud_auth_mark_revoked(void) {
    const esp_err_t result = device_binding_client_clear_session_token();
    cloud_auth_set_state(CLOUD_AUTH_STATE_REVOKED);
    return result;
}

esp_err_t cloud_auth_set_state_callback(
    cloud_auth_state_callback_t callback,
    void *context
) {
    cloud_auth_callback = callback;
    cloud_auth_callback_context = context;
    return ESP_OK;
}

const module_descriptor_t *cloud_auth_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "cloud_auth",
        .version = "1.0.0",
        .initialize = cloud_auth_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
