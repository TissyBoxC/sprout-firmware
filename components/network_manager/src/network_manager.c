#include "network_manager.h"

#include <string.h>

#include "config_store.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define NETWORK_MANAGER_WIFI_KEY_SSID "wifi_ssid"
#define NETWORK_MANAGER_WIFI_KEY_PASSWORD "wifi_password"
#define NETWORK_MANAGER_MAXIMUM_RETRY 0

static const char *const TAG = "network_manager";

static bool network_manager_ready;
static bool network_manager_started;
static network_manager_state_t network_manager_state =
    NETWORK_MANAGER_STATE_IDLE;
static network_manager_state_callback_t
    network_manager_callbacks[NETWORK_MANAGER_MAX_CALLBACKS];
static void *network_manager_callback_contexts[NETWORK_MANAGER_MAX_CALLBACKS];
static size_t network_manager_callback_count;
static esp_netif_t *network_manager_station_netif;

static void network_manager_publish_state(network_manager_state_t state) {
    if (network_manager_state == state) {
        return;
    }
    network_manager_state = state;
    for (size_t index = 0; index < network_manager_callback_count; ++index) {
        if (network_manager_callbacks[index] != NULL) {
            network_manager_callbacks[index](
                state,
                network_manager_callback_contexts[index]
            );
        }
    }
}

static void network_manager_wifi_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    (void)argument;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        network_manager_publish_state(NETWORK_MANAGER_STATE_IDLE);
        // Credentials are kept, so reconnecting is safe and recovers from a
        // router reboot without asking the guardian to provision again.
        const esp_err_t result = esp_wifi_connect();
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "wifi reconnect failed: %s", esp_err_to_name(result));
        } else {
            network_manager_publish_state(NETWORK_MANAGER_STATE_CONNECTING);
        }
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        network_manager_publish_state(NETWORK_MANAGER_STATE_CONNECTED);
    }
}

esp_err_t network_manager_init(void) {
    if (network_manager_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = esp_netif_init();
    if (result != ESP_OK) {
        return result;
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    if (network_manager_station_netif == NULL) {
        network_manager_station_netif = esp_netif_create_default_wifi_sta();
        if (network_manager_station_netif == NULL) {
            return ESP_FAIL;
        }
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init_config);
    if (result != ESP_OK) {
        return result;
    }

    result = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        network_manager_wifi_event_handler,
        NULL,
        NULL
    );
    if (result != ESP_OK) {
        return result;
    }
    result = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        network_manager_wifi_event_handler,
        NULL,
        NULL
    );
    if (result != ESP_OK) {
        return result;
    }

    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_wifi_start();
    if (result != ESP_OK) {
        return result;
    }

    network_manager_started = true;
    network_manager_ready = true;
    return ESP_OK;
}

esp_err_t network_manager_connect(const char *ssid, const char *password) {
    if (!network_manager_ready || !network_manager_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == NULL || ssid[0] == '\0' ||
        strlen(ssid) > CONFIG_STORE_SSID_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password != NULL && strlen(password) > CONFIG_STORE_PASSWORD_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = network_manager_store_credentials(ssid, password);
    if (result != ESP_OK) {
        return result;
    }

    wifi_config_t wifi_config = {0};
    memcpy(
        wifi_config.sta.ssid,
        ssid,
        strlen(ssid)
    );
    if (password != NULL) {
        memcpy(
            wifi_config.sta.password,
            password,
            strlen(password)
        );
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    network_manager_publish_state(NETWORK_MANAGER_STATE_CONNECTING);
    result = esp_wifi_disconnect();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_CONNECT) {
        return result;
    }
    result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (result != ESP_OK) {
        return result;
    }
    return esp_wifi_connect();
}

esp_err_t network_manager_store_credentials(
    const char *ssid,
    const char *password
) {
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == NULL || ssid[0] == '\0' ||
        strlen(ssid) > CONFIG_STORE_SSID_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password != NULL && strlen(password) > CONFIG_STORE_PASSWORD_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t ssid_result = config_store_set_string(
        NETWORK_MANAGER_WIFI_KEY_SSID,
        ssid
    );
    if (ssid_result != ESP_OK) {
        return ssid_result;
    }
    return config_store_set_string(
        NETWORK_MANAGER_WIFI_KEY_PASSWORD,
        password != NULL ? password : ""
    );
}

esp_err_t network_manager_disconnect(void) {
    if (!network_manager_started) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = esp_wifi_disconnect();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_CONNECT) {
        return result;
    }
    network_manager_publish_state(NETWORK_MANAGER_STATE_IDLE);
    return ESP_OK;
}

network_manager_state_t network_manager_get_state(void) {
    return network_manager_state;
}

esp_err_t network_manager_copy_ssid(char *output, size_t output_size) {
    if (output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!network_manager_started) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_ap_record_t access_point_record = {0};
    const esp_err_t result = esp_wifi_sta_get_ap_info(&access_point_record);
    if (result != ESP_OK) {
        return result;
    }
    const size_t ssid_length = strnlen(
        (const char *)access_point_record.ssid,
        sizeof(access_point_record.ssid)
    );
    if (ssid_length + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, access_point_record.ssid, ssid_length);
    output[ssid_length] = '\0';
    return ESP_OK;
}

esp_err_t network_manager_set_state_callback(
    network_manager_state_callback_t callback,
    void *context
) {
    network_manager_callback_count = 0;
    memset(network_manager_callbacks, 0, sizeof(network_manager_callbacks));
    memset(
        network_manager_callback_contexts,
        0,
        sizeof(network_manager_callback_contexts)
    );
    if (callback == NULL) {
        return ESP_OK;
    }
    return network_manager_add_state_callback(callback, context);
}

esp_err_t network_manager_add_state_callback(
    network_manager_state_callback_t callback,
    void *context
) {
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0; index < network_manager_callback_count; ++index) {
        if (network_manager_callbacks[index] == callback &&
            network_manager_callback_contexts[index] == context) {
            return ESP_OK;
        }
    }
    if (network_manager_callback_count >= NETWORK_MANAGER_MAX_CALLBACKS) {
        return ESP_ERR_NO_MEM;
    }
    network_manager_callbacks[network_manager_callback_count] = callback;
    network_manager_callback_contexts[network_manager_callback_count] = context;
    ++network_manager_callback_count;
    return ESP_OK;
}

esp_err_t network_manager_remove_state_callback(
    network_manager_state_callback_t callback,
    void *context
) {
    for (size_t index = 0; index < network_manager_callback_count; ++index) {
        if (network_manager_callbacks[index] != callback ||
            network_manager_callback_contexts[index] != context) {
            continue;
        }
        for (size_t move_index = index + 1;
             move_index < network_manager_callback_count;
             ++move_index) {
            network_manager_callbacks[move_index - 1] =
                network_manager_callbacks[move_index];
            network_manager_callback_contexts[move_index - 1] =
                network_manager_callback_contexts[move_index];
        }
        --network_manager_callback_count;
        network_manager_callbacks[network_manager_callback_count] = NULL;
        network_manager_callback_contexts[network_manager_callback_count] = NULL;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t network_manager_reconnect_stored(void) {
    if (!network_manager_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    char ssid[CONFIG_STORE_SSID_SIZE + 1] = {0};
    char password[CONFIG_STORE_PASSWORD_SIZE + 1] = {0};
    esp_err_t result = config_store_get_string(
        NETWORK_MANAGER_WIFI_KEY_SSID,
        ssid,
        sizeof(ssid)
    );
    if (result != ESP_OK) {
        return result;
    }
    result = config_store_get_string(
        NETWORK_MANAGER_WIFI_KEY_PASSWORD,
        password,
        sizeof(password)
    );
    if (result != ESP_OK && result != CONFIG_STORE_ERR_NOT_FOUND) {
        return result;
    }

    result = network_manager_connect(ssid, password);
    memset(password, 0, sizeof(password));
    return result;
}

const module_descriptor_t *network_manager_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "network_manager",
        .version = "1.0.0",
        .initialize = network_manager_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
