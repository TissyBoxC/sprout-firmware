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
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/network_config.h"
#include "network_provisioning/scheme_ble.h"
#include "esp_srp.h"
#include "provisioning_payload.h"

#define DEVICE_PROVISIONING_SERVICE_NAME_SIZE 32
#define DEVICE_PROVISIONING_BINDING_URI_SIZE PROVISIONING_PAYLOAD_SIZE
#define DEVICE_PROVISIONING_SETUP_URI_SIZE PROVISIONING_PAYLOAD_SIZE
#define DEVICE_PROVISIONING_POLL_INTERVAL_MS 2000
#define DEVICE_PROVISIONING_SALT_KEY "provisioning_srp_salt"
#define DEVICE_PROVISIONING_VERIFIER_KEY "provisioning_srp_verifier"
#define DEVICE_PROVISIONING_POP_KEY "provisioning_pop"
#define DEVICE_PROVISIONING_SALT_SIZE 16
#define DEVICE_PROVISIONING_SALT_MAX_SIZE 64
#define DEVICE_PROVISIONING_VERIFIER_MAX_SIZE 384
#define DEVICE_PROVISIONING_POP_TEXT_SIZE 9
#define DEVICE_PROVISIONING_CREDENTIAL_RETRY_COUNT 8

static const char *const TAG = "device_provisioning";
static const char *const DEVICE_PROVISIONING_CUSTOM_ENDPOINT = "custom-data";

static bool device_provisioning_is_ready;
static bool device_provisioning_is_service_active;
static bool device_provisioning_has_pending_credentials;
static bool device_provisioning_should_prepare_binding;
static bool device_provisioning_has_security_credentials;
static char device_provisioning_binding_uri[DEVICE_PROVISIONING_BINDING_URI_SIZE];
static char device_provisioning_setup_uri[DEVICE_PROVISIONING_SETUP_URI_SIZE];
static char device_provisioning_service_name[DEVICE_PROVISIONING_SERVICE_NAME_SIZE];
static char
    device_provisioning_pending_ssid[CONFIG_STORE_SSID_SIZE + 1];
static char
    device_provisioning_pending_password[CONFIG_STORE_PASSWORD_SIZE + 1];
static char device_provisioning_salt[DEVICE_PROVISIONING_SALT_MAX_SIZE];
static char
    device_provisioning_verifier[DEVICE_PROVISIONING_VERIFIER_MAX_SIZE];
static char device_provisioning_proof_of_possession[
    DEVICE_PROVISIONING_POP_TEXT_SIZE];
static network_prov_security2_params_t device_provisioning_security_parameters;
static SemaphoreHandle_t device_provisioning_state_mutex;
static device_provisioning_event_callback_t device_provisioning_event_callback;
static void *device_provisioning_event_context;

static void device_provisioning_rotate_security_credentials(void);

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

/**
 * @brief Generate and persist a unique proof of possession for this device.
 *
 * The SRP verifier is derived from the generated proof and the shared protocol
 * username. Only the verifier and the proof needed by the local setup screen
 * are stored; the platform never receives either value.
 */
static esp_err_t device_provisioning_generate_security_credentials(void) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    char proof_of_possession[DEVICE_PROVISIONING_POP_TEXT_SIZE] = {0};
    for (size_t index = 0;
         index + 1 < sizeof(proof_of_possession);
         ++index) {
        proof_of_possession[index] =
            alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }

    const char *const username = provisioning_payload_security_username();
    char *salt = nullptr;
    char *verifier = nullptr;
    int verifier_size = 0;
    esp_err_t result = ESP_FAIL;
    for (int attempt = 0;
         attempt < DEVICE_PROVISIONING_CREDENTIAL_RETRY_COUNT;
         ++attempt) {
        free(salt);
        free(verifier);
        salt = nullptr;
        verifier = nullptr;
        verifier_size = 0;
        result = esp_srp_gen_salt_verifier(
            username,
            (int)strlen(username),
            proof_of_possession,
            (int)strlen(proof_of_possession),
            &salt,
            DEVICE_PROVISIONING_SALT_SIZE,
            &verifier,
            &verifier_size
        );
        // The API does not return the salt byte length. ESP-IDF encodes the
        // salt as a big-endian integer, so a low leading bit would be lost if
        // it were stored as a fixed-width blob. Retry until the generated
        // value is guaranteed to use all configured bytes.
        if (result == ESP_OK && salt != nullptr &&
            (((unsigned char)salt[0]) & 0x80) != 0) {
            break;
        }
        result = result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
    }
    if (result != ESP_OK || salt == nullptr || verifier == nullptr ||
        verifier_size <= 0 ||
        (size_t)verifier_size > DEVICE_PROVISIONING_VERIFIER_MAX_SIZE) {
        free(salt);
        free(verifier);
        memset(proof_of_possession, 0, sizeof(proof_of_possession));
        return result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
    }

    result = config_store_set_blob(
        DEVICE_PROVISIONING_SALT_KEY,
        salt,
        DEVICE_PROVISIONING_SALT_SIZE
    );
    if (result == ESP_OK) {
        result = config_store_set_blob(
            DEVICE_PROVISIONING_VERIFIER_KEY,
            verifier,
            (size_t)verifier_size
        );
    }
    if (result == ESP_OK) {
        result = config_store_set_string(
            DEVICE_PROVISIONING_POP_KEY,
            proof_of_possession
        );
    }
    memset(proof_of_possession, 0, sizeof(proof_of_possession));
    free(salt);
    free(verifier);
    if (result == ESP_OK) {
        device_provisioning_has_security_credentials = true;
    }
    return result;
}

static esp_err_t device_provisioning_build_setup_uri(void) {
    if (device_provisioning_service_name[0] == '\0' ||
        device_provisioning_proof_of_possession[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    return provisioning_payload_build_setup_qr(
        device_provisioning_service_name,
        device_provisioning_proof_of_possession,
        device_provisioning_setup_uri,
        sizeof(device_provisioning_setup_uri)
    );
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
        if (device_provisioning_event_callback != nullptr) {
            device_provisioning_event_callback(
                DEVICE_PROVISIONING_EVENT_STARTED,
                "ble_service_started",
                device_provisioning_event_context
            );
        }
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
        if (device_provisioning_event_callback != nullptr) {
            device_provisioning_event_callback(
                DEVICE_PROVISIONING_EVENT_WIFI_CONFIGURED,
                "credentials_accepted",
                device_provisioning_event_context
            );
        }
        return;
    }
    if (event == NETWORK_PROV_WIFI_CRED_FAIL && event_data != nullptr) {
        const network_prov_wifi_sta_fail_reason_t reason =
            *static_cast<const network_prov_wifi_sta_fail_reason_t *>(
                event_data
            );
        const char *detail_code =
            reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR
                ? "authentication_error"
                : "access_point_not_found";
        if (device_provisioning_event_callback != nullptr) {
            device_provisioning_event_callback(
                DEVICE_PROVISIONING_EVENT_WIFI_FAILED,
                detail_code,
                device_provisioning_event_context
            );
        }
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
    esp_err_t last_auth_error = ESP_OK;

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
            esp_err_t auth_result = ESP_OK;
            if (!device_binding_client_is_registered()) {
                auth_result = device_binding_client_register_pending();
            }
            if (auth_result == ESP_OK) {
                auth_result = device_binding_client_authenticate();
            }
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
                if (auth_result != last_auth_error) {
                    ESP_LOGW(
                        TAG,
                        "device authentication failed: %s",
                        esp_err_to_name(auth_result)
                    );
                    last_auth_error = auth_result;
                }
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

    device_provisioning_rotate_security_credentials();
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
    size_t verifier_size = sizeof(device_provisioning_verifier);
    const esp_err_t verifier_read_result = config_store_get_blob(
        DEVICE_PROVISIONING_VERIFIER_KEY,
        device_provisioning_verifier,
        &verifier_size
    );
    esp_err_t proof_read_result = config_store_get_string(
        DEVICE_PROVISIONING_POP_KEY,
        device_provisioning_proof_of_possession,
        sizeof(device_provisioning_proof_of_possession)
    );
    const bool is_usable = result == ESP_OK &&
                           verifier_read_result == ESP_OK &&
                           proof_read_result == ESP_OK &&
                           salt_size > 0 && verifier_size > 0;
    if (!is_usable) {
        // First boot or an incomplete manufacturing image must still produce a
        // usable local credential instead of falling back to a shared secret.
        const esp_err_t generate_result =
            device_provisioning_generate_security_credentials();
        if (generate_result != ESP_OK) {
            return generate_result;
        }
        salt_size = sizeof(device_provisioning_salt);
        result = config_store_get_blob(
            DEVICE_PROVISIONING_SALT_KEY,
            device_provisioning_salt,
            &salt_size
        );
        verifier_size = sizeof(device_provisioning_verifier);
        const esp_err_t verifier_result = config_store_get_blob(
            DEVICE_PROVISIONING_VERIFIER_KEY,
            device_provisioning_verifier,
            &verifier_size
        );
        proof_read_result = config_store_get_string(
            DEVICE_PROVISIONING_POP_KEY,
            device_provisioning_proof_of_possession,
            sizeof(device_provisioning_proof_of_possession)
        );
        if (result != ESP_OK || verifier_result != ESP_OK ||
            proof_read_result != ESP_OK) {
            return result != ESP_OK ? result : verifier_result;
        }
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
    device_provisioning_has_security_credentials = true;
    return ESP_OK;
}

/**
 * @brief Erase the one-time local provisioning secret after a successful bind.
 *
 * A photographed setup code must not stay valid after the device has been
 * provisioned. A fresh proof of possession and SRP verifier are generated on
 * the next provisioning start.
 */
static void device_provisioning_rotate_security_credentials(void) {
    config_store_erase_key(DEVICE_PROVISIONING_SALT_KEY);
    config_store_erase_key(DEVICE_PROVISIONING_VERIFIER_KEY);
    config_store_erase_key(DEVICE_PROVISIONING_POP_KEY);
    memset(device_provisioning_salt, 0, sizeof(device_provisioning_salt));
    memset(
        device_provisioning_verifier,
        0,
        sizeof(device_provisioning_verifier)
    );
    memset(
        device_provisioning_proof_of_possession,
        0,
        sizeof(device_provisioning_proof_of_possession)
    );
    memset(
        &device_provisioning_security_parameters,
        0,
        sizeof(device_provisioning_security_parameters)
    );
    device_provisioning_has_security_credentials = false;
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
    if (!device_provisioning_has_security_credentials) {
        const esp_err_t credentials_result =
            device_provisioning_load_security_credentials();
        if (credentials_result != ESP_OK) {
            return credentials_result;
        }
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

    result = device_provisioning_build_setup_uri();
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
    // First boot derives a unique local credential before BLE is advertised.
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

esp_err_t device_provisioning_copy_setup_payload(
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!device_provisioning_is_service_active ||
        device_provisioning_setup_uri[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t length = strlen(device_provisioning_setup_uri);
    if (length + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, device_provisioning_setup_uri, length + 1);
    return ESP_OK;
}

esp_err_t device_provisioning_set_event_callback(
    device_provisioning_event_callback_t callback,
    void *context
) {
    device_provisioning_event_callback = callback;
    device_provisioning_event_context = context;
    return ESP_OK;
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
