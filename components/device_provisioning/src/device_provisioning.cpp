// ESP-IDF's provisioning stack and protocomm callbacks are C++-compiled.
// Keep this module in one translation unit and export C symbols to the C
// composition root.
#include "device_provisioning.h"

extern "C" {

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "config_store.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/network_config.h"
#include "network_provisioning/scheme_ble.h"
#include "provisioning_payload.h"

#define DEVICE_PROVISIONING_SERVICE_NAME_SIZE 32
#define DEVICE_PROVISIONING_BINDING_URI_SIZE PROVISIONING_PAYLOAD_SIZE
#define DEVICE_PROVISIONING_POLL_INTERVAL_MS 2000
#define DEVICE_PROVISIONING_SALT_KEY "provisioning_srp_salt"
#define DEVICE_PROVISIONING_VERIFIER_KEY "provisioning_srp_verifier"
#define DEVICE_PROVISIONING_SALT_MAX_SIZE 64
#define DEVICE_PROVISIONING_VERIFIER_MAX_SIZE 256

static const char *const TAG = "device_provisioning";
static const char *const DEVICE_PROVISIONING_CUSTOM_ENDPOINT = "custom-data";

static bool device_provisioning_is_ready;
static bool device_provisioning_is_service_active;
static bool device_provisioning_has_pending_credentials;
static bool device_provisioning_should_prepare_binding;
static char device_provisioning_binding_uri[DEVICE_PROVISIONING_BINDING_URI_SIZE];
static char device_provisioning_service_name[DEVICE_PROVISIONING_SERVICE_NAME_SIZE];
static char
    device_provisioning_pending_ssid[CONFIG_STORE_SSID_SIZE + 1];
static char
    device_provisioning_pending_password[CONFIG_STORE_PASSWORD_SIZE + 1];
static char device_provisioning_salt[DEVICE_PROVISIONING_SALT_MAX_SIZE];
static char
    device_provisioning_verifier[DEVICE_PROVISIONING_VERIFIER_MAX_SIZE];
static network_prov_security2_params_t device_provisioning_security_parameters;
static SemaphoreHandle_t device_provisioning_state_mutex;

/**
 * @brief Build a nearby-device name unique within BLE range.
 *
 * The MAC suffix is used only for the local advertisement. It never replaces
 * the platform device identifier or appears in the binding URI.
 */
static esp_err_t device_provisioning_build_service_name(
    char *output,
    size_t output_size
) {
    uint8_t mac[6] = {0};
    const esp_err_t result = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (result != ESP_OK) {
        return result;
    }
    const int written = snprintf(
        output,
        output_size,
        "%s-%02X%02X%02X",
        CONFIG_DEVICE_PROVISIONING_SERVICE_PREFIX,
        mac[3],
        mac[4],
        mac[5]
    );
    if (written <= 0 || (size_t)written >= output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

/**
 * @brief Build the one-time URI returned through the custom-data endpoint.
 *
 * A new token is requested for each provisioning start. The token is consumed
 * by exactly one guardian binding request and is never written to logs.
 */
static esp_err_t device_provisioning_build_binding_uri(const char *device_name) {
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    esp_err_t result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        return result;
    }

    char binding_token[DEVICE_BINDING_TOKEN_SIZE] = {0};
    result = device_binding_client_create_provisioning_token(
        binding_token,
        sizeof(binding_token)
    );
    if (result != ESP_OK) {
        return result;
    }

    if (xSemaphoreTake(
            device_provisioning_state_mutex,
            portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    result = provisioning_payload_build_qr(
        device_id,
        binding_token,
        device_name,
        device_provisioning_binding_uri,
        sizeof(device_provisioning_binding_uri)
    );
    xSemaphoreGive(device_provisioning_state_mutex);
    memset(binding_token, 0, sizeof(binding_token));
    if (result != ESP_OK) {
        return result;
    }
    return ESP_OK;
}

static esp_err_t device_provisioning_custom_data_handler(
    uint32_t session_id,
    const uint8_t *input,
    ssize_t input_length,
    uint8_t **output,
    ssize_t *output_length,
    void *private_data
) {
    (void)session_id;
    (void)input;
    (void)input_length;
    (void)private_data;

    if (device_provisioning_state_mutex == nullptr ||
        xSemaphoreTake(
            device_provisioning_state_mutex,
            portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (device_provisioning_binding_uri[0] == '\0') {
        xSemaphoreGive(device_provisioning_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    char *const response = strdup(device_provisioning_binding_uri);
    const size_t response_length = strlen(device_provisioning_binding_uri);
    xSemaphoreGive(device_provisioning_state_mutex);
    *output = (uint8_t *)response;
    if (*output == nullptr) {
        *output_length = 0;
        return ESP_ERR_NO_MEM;
    }
    *output_length = (ssize_t)response_length;
    return ESP_OK;
}

static void device_provisioning_event_handler(
    void *user_data,
    network_prov_cb_event_t event,
    void *event_data
) {
    (void)user_data;
    (void)event_data;
    if (event == NETWORK_PROV_START) {
        device_provisioning_is_service_active = true;
        return;
    }
    if (event == NETWORK_PROV_END) {
        device_provisioning_is_service_active = false;
        return;
    }
    if (event == NETWORK_PROV_WIFI_CRED_RECV && event_data != nullptr) {
        const wifi_sta_config_t *const station =
            static_cast<const wifi_sta_config_t *>(event_data);
        const size_t ssid_length = strnlen(
            reinterpret_cast<const char *>(station->ssid),
            sizeof(station->ssid)
        );
        const size_t password_length = strnlen(
            reinterpret_cast<const char *>(station->password),
            sizeof(station->password)
        );
        char ssid[CONFIG_STORE_SSID_SIZE + 1] = {0};
        char password[CONFIG_STORE_PASSWORD_SIZE + 1] = {0};
        if (ssid_length > CONFIG_STORE_SSID_SIZE ||
            password_length > CONFIG_STORE_PASSWORD_SIZE) {
            return;
        }
        memcpy(ssid, station->ssid, ssid_length);
        memcpy(password, station->password, password_length);
        if (device_provisioning_state_mutex == nullptr ||
            xSemaphoreTake(
                device_provisioning_state_mutex,
                portMAX_DELAY) != pdTRUE) {
            return;
        }
        memcpy(device_provisioning_pending_ssid, ssid, ssid_length + 1);
        memcpy(
            device_provisioning_pending_password,
            password,
            password_length + 1
        );
        device_provisioning_has_pending_credentials = true;
        xSemaphoreGive(device_provisioning_state_mutex);
        memset(password, 0, sizeof(password));
        return;
    }
    if (event == NETWORK_PROV_WIFI_CRED_SUCCESS &&
        device_provisioning_state_mutex != nullptr &&
        xSemaphoreTake(
            device_provisioning_state_mutex,
            portMAX_DELAY) == pdTRUE) {
        device_provisioning_should_prepare_binding = true;
        xSemaphoreGive(device_provisioning_state_mutex);
    }
}

/**
 * @brief Authenticate, publish the binding URI, and wait for guardian binding.
 *
 * The work task does not block the provisioning manager callbacks while HTTPS
 * authentication and platform token creation are in progress. Once the
 * guardian completes binding, it stops BLE and hands Wi-Fi back to
 * network_manager.
 *
 * The cleanup delay lets the final custom-data response leave the device
 * before BLE is torn down. network_manager reconnects the saved network only
 * after the provisioning manager has released Wi-Fi ownership.
 */
static void device_provisioning_work_task_entry(void *argument) {
    (void)argument;
    bool is_binding_uri_ready = false;

    while (!device_binding_client_is_bound()) {
        bool should_prepare_binding = false;
        if (device_provisioning_state_mutex != nullptr &&
            xSemaphoreTake(
                device_provisioning_state_mutex,
                portMAX_DELAY) == pdTRUE) {
            should_prepare_binding =
                device_provisioning_should_prepare_binding;
            xSemaphoreGive(device_provisioning_state_mutex);
        }

        char pending_ssid[CONFIG_STORE_SSID_SIZE + 1] = {0};
        char pending_password[CONFIG_STORE_PASSWORD_SIZE + 1] = {0};
        bool has_pending_credentials = false;
        if (device_provisioning_state_mutex != nullptr &&
            xSemaphoreTake(
                device_provisioning_state_mutex,
                portMAX_DELAY) == pdTRUE) {
            has_pending_credentials =
                device_provisioning_has_pending_credentials;
            if (has_pending_credentials) {
                memcpy(
                    pending_ssid,
                    device_provisioning_pending_ssid,
                    sizeof(pending_ssid)
                );
                memcpy(
                    pending_password,
                    device_provisioning_pending_password,
                    sizeof(pending_password)
                );
                device_provisioning_has_pending_credentials = false;
                memset(
                    device_provisioning_pending_password,
                    0,
                    sizeof(device_provisioning_pending_password)
                );
            }
            xSemaphoreGive(device_provisioning_state_mutex);
        }
        if (has_pending_credentials) {
            const esp_err_t store_result =
                network_manager_store_credentials(
                    pending_ssid,
                    pending_password
                );
            if (store_result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "credential persistence failed: %s",
                    esp_err_to_name(store_result)
                );
                // Keep the pending copy so a transient NVS failure does not
                // discard the guardian's network settings.
                if (device_provisioning_state_mutex != nullptr &&
                    xSemaphoreTake(
                        device_provisioning_state_mutex,
                        portMAX_DELAY) == pdTRUE) {
                    memcpy(
                        device_provisioning_pending_ssid,
                        pending_ssid,
                        sizeof(device_provisioning_pending_ssid)
                    );
                    memcpy(
                        device_provisioning_pending_password,
                        pending_password,
                        sizeof(device_provisioning_pending_password)
                    );
                    device_provisioning_has_pending_credentials = true;
                    xSemaphoreGive(device_provisioning_state_mutex);
                }
                vTaskDelay(
                    pdMS_TO_TICKS(DEVICE_PROVISIONING_POLL_INTERVAL_MS)
                );
            }
            memset(pending_password, 0, sizeof(pending_password));
            if (store_result != ESP_OK) {
                continue;
            }
        }

        if (!is_binding_uri_ready && should_prepare_binding) {
            const esp_err_t auth_result =
                device_binding_client_authenticate();
            if (auth_result == ESP_OK) {
                const esp_err_t uri_result =
                    device_provisioning_build_binding_uri(
                        device_provisioning_service_name
                    );
                if (uri_result == ESP_OK) {
                    is_binding_uri_ready = true;
                } else {
                    ESP_LOGW(
                        TAG,
                        "binding URI unavailable: %s",
                        esp_err_to_name(uri_result)
                    );
                }
            } else {
                ESP_LOGW(
                    TAG,
                    "device authentication failed: %s",
                    esp_err_to_name(auth_result)
                );
            }
        }

        if (is_binding_uri_ready) {
            bool is_bound = false;
            const esp_err_t binding_result =
                device_binding_client_check_binding(&is_bound);
            if (binding_result != ESP_OK &&
                binding_result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "binding status check failed: %s",
                    esp_err_to_name(binding_result)
                );
            }
            if (is_bound) {
                break;
            }
        }

        if (!is_binding_uri_ready && !has_pending_credentials) {
            vTaskDelay(
                pdMS_TO_TICKS(DEVICE_PROVISIONING_POLL_INTERVAL_MS)
            );
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(DEVICE_PROVISIONING_POLL_INTERVAL_MS));
    }

    network_prov_mgr_stop_provisioning();
    network_prov_mgr_wait();
    const esp_err_t deinit_result = network_prov_mgr_deinit();
    if (deinit_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "provisioning cleanup failed: %s",
            esp_err_to_name(deinit_result)
        );
    }
    const esp_err_t reconnect_result = network_manager_reconnect_stored();
    if (reconnect_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "stored-network reconnect failed: %s",
            esp_err_to_name(reconnect_result)
        );
    }
    vTaskDelete(nullptr);
}

static esp_err_t device_provisioning_load_security_credentials(void) {
    size_t salt_size = sizeof(device_provisioning_salt);
    esp_err_t result = config_store_get_blob(
        DEVICE_PROVISIONING_SALT_KEY,
        device_provisioning_salt,
        &salt_size
    );
    if (result != ESP_OK) {
        return result;
    }
    size_t verifier_size = sizeof(device_provisioning_verifier);
    result = config_store_get_blob(
        DEVICE_PROVISIONING_VERIFIER_KEY,
        device_provisioning_verifier,
        &verifier_size
    );
    if (result != ESP_OK) {
        return result;
    }
    if (salt_size == 0 || verifier_size == 0 ||
        salt_size > UINT16_MAX || verifier_size > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    device_provisioning_security_parameters.salt =
        device_provisioning_salt;
    device_provisioning_security_parameters.salt_len =
        static_cast<uint16_t>(salt_size);
    device_provisioning_security_parameters.verifier =
        device_provisioning_verifier;
    device_provisioning_security_parameters.verifier_len =
        static_cast<uint16_t>(verifier_size);
    return ESP_OK;
}

/**
 * @brief Start the provisioning manager and expose the binding endpoint.
 *
 * The manager owns the BLE transport. Wi-Fi credentials are applied by the
 * official manager, while this module only adds the binding URI and status
 * polling required by the guardian application.
 */
static esp_err_t device_provisioning_start_service(void) {
    esp_err_t result = device_binding_client_refresh_state();
    if (result != ESP_OK) {
        return result;
    }
    if (device_binding_client_is_bound()) {
        return network_manager_reconnect_stored();
    }
    if (!device_binding_client_is_registered()) {
        return ESP_ERR_INVALID_STATE;
    }

    result = device_provisioning_build_service_name(
        device_provisioning_service_name,
        sizeof(device_provisioning_service_name)
    );
    if (result != ESP_OK) {
        return result;
    }

    network_prov_mgr_config_t provisioning_configuration = {};
    provisioning_configuration.scheme = network_prov_scheme_ble;
    provisioning_configuration.scheme_event_handler =
        NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    provisioning_configuration.app_event_handler.event_cb =
        device_provisioning_event_handler;
    result = network_prov_mgr_init(provisioning_configuration);
    if (result != ESP_OK) {
        return result;
    }

    result = network_prov_mgr_endpoint_create(
        DEVICE_PROVISIONING_CUSTOM_ENDPOINT
    );
    if (result != ESP_OK) {
        network_prov_mgr_deinit();
        return result;
    }
    result = network_prov_mgr_disable_auto_stop(1000);
    if (result != ESP_OK) {
        network_prov_mgr_deinit();
        return result;
    }
    result = network_prov_mgr_start_provisioning(
        NETWORK_PROV_SECURITY_2,
        &device_provisioning_security_parameters,
        device_provisioning_service_name,
        nullptr
    );
    if (result != ESP_OK) {
        network_prov_mgr_deinit();
        return result;
    }
    result = network_prov_mgr_endpoint_register(
        DEVICE_PROVISIONING_CUSTOM_ENDPOINT,
        device_provisioning_custom_data_handler,
        nullptr
    );
    if (result != ESP_OK) {
        network_prov_mgr_stop_provisioning();
        return result;
    }

    if (xTaskCreate(
            device_provisioning_work_task_entry,
            "sprout_prov_work",
            4096,
            nullptr,
            4,
            nullptr) != pdPASS) {
        network_prov_mgr_stop_provisioning();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t device_provisioning_init(void) {
    if (device_provisioning_is_ready) {
        return ESP_OK;
    }
    // Missing manufacturing material deliberately keeps BLE provisioning off
    // instead of falling back to a shared or empty proof of possession.
    const esp_err_t credential_result =
        device_provisioning_load_security_credentials();
    if (credential_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "provisioning credentials unavailable: %s",
            esp_err_to_name(credential_result)
        );
        return credential_result;
    }
    if (device_provisioning_state_mutex == nullptr) {
        device_provisioning_state_mutex = xSemaphoreCreateMutex();
        if (device_provisioning_state_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }
    const esp_err_t result = device_provisioning_start_service();
    if (result != ESP_OK) {
        return result;
    }
    device_provisioning_is_ready = true;
    return ESP_OK;
}

bool device_provisioning_is_active(void) {
    return device_provisioning_is_service_active;
}

const module_descriptor_t *device_provisioning_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "device_provisioning",
        .version = "1.0.0",
        .initialize = device_provisioning_init,
        .shutdown = nullptr,
    };
    return &descriptor;
}

}  // extern "C"
