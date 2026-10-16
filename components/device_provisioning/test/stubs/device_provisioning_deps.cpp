#include "device_provisioning_host.h"

#include "config_store.h"
#include "device_binding_client.h"
#include "device_provisioning.h"
#include "device_identity.h"
#include "provisioning_payload.h"

#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_provisioning/manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {

typedef struct {
    char key[CONFIG_STORE_KEY_MAX_SIZE + 1];
    unsigned char value[CONFIG_STORE_VALUE_SIZE];
    size_t value_size;
    bool is_blob;
    bool used;
} host_config_entry_t;

#define HOST_CONFIG_CAPACITY 16

static host_config_entry_t host_config[HOST_CONFIG_CAPACITY];
static uint8_t host_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static esp_err_t host_binding_refresh_result = ESP_OK;
static bool host_bound;
static bool host_registered = true;
static esp_err_t host_register_result = ESP_OK;
static esp_err_t host_authenticate_result = ESP_OK;
static esp_err_t host_create_token_result = ESP_OK;
static esp_err_t host_check_binding_result = ESP_OK;
static esp_err_t host_store_credentials_result = ESP_OK;
static esp_err_t host_reconnect_result = ESP_OK;
static uint8_t host_random_values[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
static size_t host_random_size = sizeof(host_random_values);
static esp_err_t host_srp_result = ESP_OK;
static uint8_t host_srp_salt_leading_byte = 0x90;
static int host_srp_verifier_size = 128;
static esp_err_t host_config_get_blob_result = CONFIG_STORE_ERR_NOT_FOUND;
static esp_err_t host_config_get_string_result = CONFIG_STORE_ERR_NOT_FOUND;
static bool host_config_get_blob_forced;
static bool host_config_get_string_forced;
static esp_err_t host_config_set_blob_result = ESP_OK;
static esp_err_t host_config_set_string_result = ESP_OK;
static int host_task_create_result = 1;
static esp_err_t host_manager_init_result = ESP_OK;
static esp_err_t host_endpoint_create_result = ESP_OK;
static esp_err_t host_disable_auto_stop_result = ESP_OK;
static esp_err_t host_start_result = ESP_OK;
static esp_err_t host_endpoint_register_result = ESP_OK;
static esp_err_t host_deinit_result = ESP_OK;
static int host_srp_calls;
static int host_config_get_blob_calls;
static int host_config_get_string_calls;
static int host_config_set_blob_calls;
static int host_config_set_string_calls;
static int host_config_erase_calls;
static int host_register_calls;
static int host_authenticate_calls;
static int host_create_token_calls;
static int host_check_binding_calls;
static int host_store_credentials_calls;
static int host_reconnect_calls;
static int host_manager_init_calls;
static int host_endpoint_create_calls;
static int host_disable_auto_stop_calls;
static int host_start_calls;
static int host_endpoint_register_calls;
static int host_stop_calls;
static int host_deinit_calls;
static int host_wait_calls;
static int host_task_create_calls;
static int host_task_delete_calls;
static int host_delay_calls;
static bool host_task_pending;
static TaskFunction_t host_pending_task;
static bool host_last_started_security2;
static bool host_last_wifi_configured;
static bool host_last_wifi_failed;
static bool host_last_should_prepare_binding;
static bool host_event_callback_present;
static network_prov_cb_event_t host_last_event;
static esp_err_t (*host_endpoint_handler)(
    uint32_t session_id,
    const uint8_t *input,
    ssize_t input_length,
    uint8_t **output,
    ssize_t *output_length,
    void *private_data
);
static char host_service_name[64];
static char host_security_salt[64];
static size_t host_security_salt_size;
static char host_security_verifier[384];
static size_t host_security_verifier_size;
static char host_last_endpoint[64];
static char host_registered_token[DEVICE_BINDING_TOKEN_SIZE];
static char host_last_detail_code[64];
static int host_event_callback_calls;
static char host_callback_detail[64];
static device_provisioning_event_t host_callback_event;

static host_config_entry_t *host_find(const char *key) {
    for (size_t index = 0; index < HOST_CONFIG_CAPACITY; ++index) {
        if (host_config[index].used &&
            strcmp(host_config[index].key, key) == 0) {
            return &host_config[index];
        }
    }
    return nullptr;
}

static host_config_entry_t *host_allocate(const char *key) {
    host_config_entry_t *entry = host_find(key);
    if (entry != nullptr) {
        return entry;
    }
    for (size_t index = 0; index < HOST_CONFIG_CAPACITY; ++index) {
        if (!host_config[index].used) {
            memset(&host_config[index], 0, sizeof(host_config[index]));
            host_config[index].used = true;
            strncpy(
                host_config[index].key,
                key,
                sizeof(host_config[index].key) - 1
            );
            host_config[index].key[sizeof(host_config[index].key) - 1] = '\0';
            return &host_config[index];
        }
    }
    return nullptr;
}

void device_provisioning_host_reset(void) {
    memset(host_config, 0, sizeof(host_config));
    memcpy(host_mac, (uint8_t[]){0x00, 0x11, 0x22, 0x33, 0x44, 0x55}, 6);
    host_binding_refresh_result = ESP_OK;
    host_bound = false;
    host_registered = true;
    host_register_result = ESP_OK;
    host_authenticate_result = ESP_OK;
    host_create_token_result = ESP_OK;
    host_check_binding_result = ESP_OK;
    host_store_credentials_result = ESP_OK;
    host_reconnect_result = ESP_OK;
    host_random_size = sizeof(host_random_values);
    host_srp_result = ESP_OK;
    host_srp_salt_leading_byte = 0x90;
    host_srp_verifier_size = 128;
    host_config_get_blob_result = CONFIG_STORE_ERR_NOT_FOUND;
    host_config_get_string_result = CONFIG_STORE_ERR_NOT_FOUND;
    host_config_get_blob_forced = false;
    host_config_get_string_forced = false;
    host_config_set_blob_result = ESP_OK;
    host_config_set_string_result = ESP_OK;
    host_task_create_result = 1;
    host_manager_init_result = ESP_OK;
    host_endpoint_create_result = ESP_OK;
    host_disable_auto_stop_result = ESP_OK;
    host_start_result = ESP_OK;
    host_endpoint_register_result = ESP_OK;
    host_deinit_result = ESP_OK;
    host_srp_calls = 0;
    host_config_get_blob_calls = 0;
    host_config_get_string_calls = 0;
    host_config_set_blob_calls = 0;
    host_config_set_string_calls = 0;
    host_config_erase_calls = 0;
    host_register_calls = 0;
    host_authenticate_calls = 0;
    host_create_token_calls = 0;
    host_check_binding_calls = 0;
    host_store_credentials_calls = 0;
    host_reconnect_calls = 0;
    host_manager_init_calls = 0;
    host_endpoint_create_calls = 0;
    host_disable_auto_stop_calls = 0;
    host_start_calls = 0;
    host_endpoint_register_calls = 0;
    host_stop_calls = 0;
    host_deinit_calls = 0;
    host_wait_calls = 0;
    host_task_create_calls = 0;
    host_task_delete_calls = 0;
    host_delay_calls = 0;
    host_task_pending = false;
    host_last_started_security2 = false;
    host_last_wifi_configured = false;
    host_last_wifi_failed = false;
    host_last_should_prepare_binding = false;
    host_event_callback_present = false;
    host_last_event = static_cast<network_prov_cb_event_t>(0);
    memset(host_last_endpoint, 0, sizeof(host_last_endpoint));
    memset(host_registered_token, 0, sizeof(host_registered_token));
    memset(host_last_detail_code, 0, sizeof(host_last_detail_code));
    host_event_callback_calls = 0;
    memset(host_callback_detail, 0, sizeof(host_callback_detail));
    host_callback_event = DEVICE_PROVISIONING_EVENT_STARTED;
    host_pending_task = nullptr;
    host_endpoint_handler = nullptr;
    memset(host_service_name, 0, sizeof(host_service_name));
    memset(host_security_salt, 0, sizeof(host_security_salt));
    host_security_salt_size = 0;
    memset(host_security_verifier, 0, sizeof(host_security_verifier));
    host_security_verifier_size = 0;
}

void device_provisioning_host_set_mac(const uint8_t mac[6]) {
    if (mac != nullptr) {
        memcpy(host_mac, mac, sizeof(host_mac));
    }
}

void device_provisioning_host_set_binding_refresh_result(esp_err_t result) {
    host_binding_refresh_result = result;
}

void device_provisioning_host_set_bound(bool bound) {
    host_bound = bound;
}

void device_provisioning_host_set_registered(bool registered) {
    host_registered = registered;
}

void device_provisioning_host_set_register_result(esp_err_t result) {
    host_register_result = result;
}

void device_provisioning_host_set_authenticate_result(esp_err_t result) {
    host_authenticate_result = result;
}

void device_provisioning_host_set_create_token_result(esp_err_t result) {
    host_create_token_result = result;
}

void device_provisioning_host_set_check_binding_result(esp_err_t result) {
    host_check_binding_result = result;
}

void device_provisioning_host_set_store_credentials_result(esp_err_t result) {
    host_store_credentials_result = result;
}

void device_provisioning_host_set_reconnect_result(esp_err_t result) {
    host_reconnect_result = result;
}

void device_provisioning_host_set_random_sequence(
    const uint8_t *values,
    size_t size
) {
    if (values != nullptr && size <= sizeof(host_random_values)) {
        memcpy(host_random_values, values, size);
        host_random_size = size;
    }
}

void device_provisioning_host_set_srp_result(esp_err_t result) {
    host_srp_result = result;
}

void device_provisioning_host_set_srp_salt_leading_byte(uint8_t value) {
    host_srp_salt_leading_byte = value;
}

void device_provisioning_host_set_srp_verifier_size(int size) {
    host_srp_verifier_size = size;
}

void device_provisioning_host_set_config_get_blob_result(esp_err_t result) {
    host_config_get_blob_result = result;
    host_config_get_blob_forced = true;
}

void device_provisioning_host_set_config_get_string_result(esp_err_t result) {
    host_config_get_string_result = result;
    host_config_get_string_forced = true;
}

void device_provisioning_host_clear_config_read_failures(void) {
    host_config_get_blob_forced = false;
    host_config_get_string_forced = false;
}

void device_provisioning_host_set_config_set_blob_result(esp_err_t result) {
    host_config_set_blob_result = result;
}

void device_provisioning_host_set_config_set_string_result(esp_err_t result) {
    host_config_set_string_result = result;
}

void device_provisioning_host_set_task_create_result(int result) {
    host_task_create_result = result;
}

void device_provisioning_host_set_manager_init_result(esp_err_t result) {
    host_manager_init_result = result;
}

void device_provisioning_host_set_endpoint_create_result(esp_err_t result) {
    host_endpoint_create_result = result;
}

void device_provisioning_host_set_disable_auto_stop_result(esp_err_t result) {
    host_disable_auto_stop_result = result;
}

void device_provisioning_host_set_start_result(esp_err_t result) {
    host_start_result = result;
}

void device_provisioning_host_set_endpoint_register_result(esp_err_t result) {
    host_endpoint_register_result = result;
}

void device_provisioning_host_set_deinit_result(esp_err_t result) {
    host_deinit_result = result;
}

bool device_provisioning_host_task_pending(void) {
    return host_task_pending;
}

bool device_provisioning_host_last_started_security2(void) {
    return host_last_started_security2;
}

bool device_provisioning_host_last_wifi_configured(void) {
    return host_last_wifi_configured;
}

bool device_provisioning_host_last_wifi_failed(void) {
    return host_last_wifi_failed;
}

bool device_provisioning_host_last_should_prepare_binding(void) {
    return host_last_should_prepare_binding;
}

bool device_provisioning_host_event_callback_present(void) {
    return host_event_callback_present;
}

const char *device_provisioning_host_last_detail_code(void) {
    return host_last_detail_code;
}

int device_provisioning_host_last_event(void) {
    return static_cast<int>(host_last_event);
}

int device_provisioning_host_event_callback_calls(void) {
    return host_event_callback_calls;
}

const char *device_provisioning_host_last_endpoint(void) {
    return host_last_endpoint;
}

const char *device_provisioning_host_registered_token(void) {
    return host_registered_token;
}

int device_provisioning_host_srp_calls(void) {
    return host_srp_calls;
}

int device_provisioning_host_config_get_blob_calls(void) {
    return host_config_get_blob_calls;
}

int device_provisioning_host_config_get_string_calls(void) {
    return host_config_get_string_calls;
}

int device_provisioning_host_config_set_blob_calls(void) {
    return host_config_set_blob_calls;
}

int device_provisioning_host_config_set_string_calls(void) {
    return host_config_set_string_calls;
}

int device_provisioning_host_config_erase_calls(void) {
    return host_config_erase_calls;
}

int device_provisioning_host_register_calls(void) {
    return host_register_calls;
}

int device_provisioning_host_authenticate_calls(void) {
    return host_authenticate_calls;
}

int device_provisioning_host_create_token_calls(void) {
    return host_create_token_calls;
}

int device_provisioning_host_check_binding_calls(void) {
    return host_check_binding_calls;
}

int device_provisioning_host_store_credentials_calls(void) {
    return host_store_credentials_calls;
}

int device_provisioning_host_reconnect_calls(void) {
    return host_reconnect_calls;
}

int device_provisioning_host_manager_init_calls(void) {
    return host_manager_init_calls;
}

int device_provisioning_host_endpoint_create_calls(void) {
    return host_endpoint_create_calls;
}

int device_provisioning_host_disable_auto_stop_calls(void) {
    return host_disable_auto_stop_calls;
}

int device_provisioning_host_start_calls(void) {
    return host_start_calls;
}

int device_provisioning_host_endpoint_register_calls(void) {
    return host_endpoint_register_calls;
}

int device_provisioning_host_stop_calls(void) {
    return host_stop_calls;
}

int device_provisioning_host_deinit_calls(void) {
    return host_deinit_calls;
}

int device_provisioning_host_wait_calls(void) {
    return host_wait_calls;
}

int device_provisioning_host_task_create_calls(void) {
    return host_task_create_calls;
}

int device_provisioning_host_task_delete_calls(void) {
    return host_task_delete_calls;
}

int device_provisioning_host_delay_calls(void) {
    return host_delay_calls;
}

const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_FAIL:
            return "ESP_FAIL";
        case ESP_ERR_NO_MEM:
            return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:
            return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:
            return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE:
            return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND:
            return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_TIMEOUT:
            return "ESP_ERR_TIMEOUT";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}

esp_err_t esp_wifi_get_mac(wifi_interface_t interface, uint8_t mac[6]) {
    (void)interface;
    if (mac == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(mac, host_mac, sizeof(host_mac));
    return ESP_OK;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return reinterpret_cast<SemaphoreHandle_t>(malloc(1));
}

BaseType_t xSemaphoreTake(
    SemaphoreHandle_t semaphore,
    TickType_t ticks
) {
    (void)ticks;
    return semaphore != nullptr ? pdTRUE : 0;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    return semaphore != nullptr ? pdTRUE : 0;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    free(semaphore);
}

BaseType_t xTaskCreate(
    TaskFunction_t task,
    const char *name,
    uint32_t stack_size,
    void *argument,
    unsigned int priority,
    void *task_handle
) {
    (void)name;
    (void)stack_size;
    (void)argument;
    (void)priority;
    (void)task_handle;
    ++host_task_create_calls;
    if (host_task_create_result != pdPASS) {
        return host_task_create_result;
    }
    host_pending_task = task;
    host_task_pending = true;
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
    (void)ticks;
    ++host_delay_calls;
}

void vTaskDelete(void *task) {
    (void)task;
    ++host_task_delete_calls;
}

const void *network_prov_scheme_ble = nullptr;

esp_err_t network_prov_mgr_init(network_prov_mgr_config_t config) {
    ++host_manager_init_calls;
    host_event_callback_present = config.app_event_handler.event_cb != nullptr;
    return host_manager_init_result;
}

esp_err_t network_prov_mgr_endpoint_create(const char *endpoint) {
    ++host_endpoint_create_calls;
    (void)endpoint;
    return host_endpoint_create_result;
}

esp_err_t network_prov_mgr_disable_auto_stop(uint32_t timeout_ms) {
    (void)timeout_ms;
    ++host_disable_auto_stop_calls;
    return host_disable_auto_stop_result;
}

esp_err_t network_prov_mgr_start_provisioning(
    network_prov_security_t security,
    const network_prov_security2_params_t *security2_params,
    const char *service_name,
    const char *service_key
) {
    (void)service_key;
    ++host_start_calls;
    if (host_start_result != ESP_OK) {
        return host_start_result;
    }
    host_last_started_security2 = security == NETWORK_PROV_SECURITY_2;
    if (service_name != nullptr) {
        strncpy(host_service_name, service_name, sizeof(host_service_name) - 1);
    }
    if (security2_params != nullptr) {
        host_security_salt_size = security2_params->salt_len;
        if (security2_params->salt != nullptr) {
            memcpy(
                host_security_salt,
                security2_params->salt,
                host_security_salt_size
            );
        }
        host_security_verifier_size = security2_params->verifier_len;
        if (security2_params->verifier != nullptr) {
            memcpy(
                host_security_verifier,
                security2_params->verifier,
                host_security_verifier_size
            );
        }
    }
    return ESP_OK;
}

esp_err_t network_prov_mgr_endpoint_register(
    const char *endpoint,
    esp_err_t (*handler)(
        uint32_t session_id,
        const uint8_t *input,
        ssize_t input_length,
        uint8_t **output,
        ssize_t *output_length,
        void *private_data
    ),
    void *user_data
) {
    (void)user_data;
    ++host_endpoint_register_calls;
    if (endpoint != nullptr) {
        strncpy(host_last_endpoint, endpoint, sizeof(host_last_endpoint) - 1);
    }
    host_endpoint_handler = handler;
    return host_endpoint_register_result;
}

esp_err_t network_prov_mgr_stop_provisioning(void) {
    ++host_stop_calls;
    return ESP_OK;
}

void network_prov_mgr_wait(void) {
    ++host_wait_calls;
}

esp_err_t network_prov_mgr_deinit(void) {
    ++host_deinit_calls;
    return host_deinit_result;
}

uint32_t esp_random(void) {
    static size_t index;
    const uint32_t value = host_random_values[index % host_random_size];
    ++index;
    return value;
}

esp_err_t esp_srp_gen_salt_verifier(
    const char *username,
    int username_length,
    const char *password,
    int password_length,
    char **salt,
    int salt_length,
    char **verifier,
    int *verifier_length
) {
    (void)username;
    (void)username_length;
    (void)password;
    (void)password_length;
    ++host_srp_calls;
    if (host_srp_result != ESP_OK) {
        return host_srp_result;
    }
    *verifier = nullptr;
    *salt = nullptr;
    *verifier_length = 0;
    *verifier_length = host_srp_verifier_size;
    if (host_srp_verifier_size > 0) {
        *verifier = static_cast<char *>(malloc((size_t)host_srp_verifier_size));
        if (*verifier != nullptr) {
            memset(*verifier, 0x5A, (size_t)host_srp_verifier_size);
        }
    }
    *salt = static_cast<char *>(malloc((size_t)salt_length));
    if (*salt != nullptr) {
        memset(*salt, 0x33, (size_t)salt_length);
        (*salt)[0] = static_cast<char>(host_srp_salt_leading_byte);
    }
    return ESP_OK;
}

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *value_size_in_out
) {
    ++host_config_get_blob_calls;
    if (host_config_get_blob_forced) {
        return host_config_get_blob_result;
    }
    const host_config_entry_t *entry = host_find(key);
    if (entry == nullptr || !entry->is_blob) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (output == nullptr || value_size_in_out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*value_size_in_out < entry->value_size) {
        *value_size_in_out = entry->value_size;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, entry->value, entry->value_size);
    *value_size_in_out = entry->value_size;
    return ESP_OK;
}

esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t value_size
) {
    ++host_config_set_blob_calls;
    if (host_config_set_blob_result != ESP_OK) {
        return host_config_set_blob_result;
    }
    host_config_entry_t *entry = host_allocate(key);
    if (entry == nullptr || value == nullptr ||
        value_size > sizeof(entry->value)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(entry->value, value, value_size);
    entry->value_size = value_size;
    entry->is_blob = true;
    return ESP_OK;
}

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    ++host_config_get_string_calls;
    if (host_config_get_string_forced) {
        return host_config_get_string_result;
    }
    const host_config_entry_t *entry = host_find(key);
    if (entry == nullptr || entry->is_blob) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (output == nullptr || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strnlen(
        reinterpret_cast<const char *>(entry->value),
        entry->value_size
    );
    if (length + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, entry->value, length + 1);
    return ESP_OK;
}

esp_err_t config_store_set_string(const char *key, const char *value) {
    ++host_config_set_string_calls;
    if (host_config_set_string_result != ESP_OK) {
        return host_config_set_string_result;
    }
    host_config_entry_t *entry = host_allocate(key);
    if (entry == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strlen(value);
    if (length + 1 > sizeof(entry->value)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(entry->value, value, length + 1);
    entry->value_size = length + 1;
    entry->is_blob = false;
    return ESP_OK;
}

esp_err_t config_store_erase_key(const char *key) {
    ++host_config_erase_calls;
    host_config_entry_t *entry = host_find(key);
    if (entry != nullptr) {
        memset(entry, 0, sizeof(*entry));
    }
    return ESP_OK;
}

esp_err_t device_identity_copy(char *output, size_t output_size) {
    const char *const device_id = "device-0001";
    const size_t length = strlen(device_id);
    if (output == nullptr || output_size < length + 1) {
        return output == nullptr || output_size == 0
                   ? ESP_ERR_INVALID_ARG
                   : ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, device_id, length + 1);
    return ESP_OK;
}

esp_err_t device_binding_client_refresh_state(void) {
    return host_binding_refresh_result;
}

bool device_binding_client_is_bound(void) {
    return host_bound;
}

bool device_binding_client_is_registered(void) {
    return host_registered;
}

esp_err_t device_binding_client_register_pending(void) {
    ++host_register_calls;
    return host_register_result;
}

esp_err_t device_binding_client_authenticate(void) {
    ++host_authenticate_calls;
    return host_authenticate_result;
}

esp_err_t device_binding_client_create_provisioning_token(
    char *output,
    size_t output_size
) {
    ++host_create_token_calls;
    if (host_create_token_result != ESP_OK) {
        return host_create_token_result;
    }
    const char *const token = "0123456789abcdef";
    if (output == nullptr || output_size < strlen(token) + 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, token, strlen(token) + 1);
    memset(host_registered_token, 0, sizeof(host_registered_token));
    memcpy(
        host_registered_token,
        output,
        strlen(output) + 1
    );
    return ESP_OK;
}

esp_err_t device_binding_client_check_binding(bool *is_bound_out) {
    ++host_check_binding_calls;
    if (is_bound_out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *is_bound_out = host_bound;
    return host_check_binding_result;
}

void device_provisioning_host_receive_event(
    network_prov_cb_event_t event,
    void *event_data
) {
    host_last_event = event;
    switch (event) {
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            host_last_wifi_configured = true;
            host_last_should_prepare_binding = true;
            break;
        case NETWORK_PROV_WIFI_CRED_FAIL:
            host_last_wifi_failed = true;
            if (event_data != nullptr) {
                const network_prov_wifi_sta_fail_reason_t reason =
                    *static_cast<const network_prov_wifi_sta_fail_reason_t *>(
                        event_data
                    );
                const char *const detail =
                    reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR
                        ? "authentication_error"
                        : "access_point_not_found";
                strncpy(
                    host_last_detail_code,
                    detail,
                    sizeof(host_last_detail_code) - 1
                );
            }
            break;
        default:
            break;
    }
}

static void host_event_callback(
    device_provisioning_event_t event,
    const char *detail_code,
    void *context
) {
    (void)context;
    ++host_event_callback_calls;
    host_callback_event = event;
    if (detail_code != nullptr) {
        strncpy(
            host_callback_detail,
            detail_code,
            sizeof(host_callback_detail) - 1
        );
        strncpy(
            host_last_detail_code,
            detail_code,
            sizeof(host_last_detail_code) - 1
        );
    }
}

void device_provisioning_host_reset_event_callback_observed(void) {
    host_event_callback_calls = 0;
    memset(host_last_detail_code, 0, sizeof(host_last_detail_code));
}

void device_provisioning_host_install_event_callback(void) {
    device_provisioning_host_reset_event_callback_observed();
    device_provisioning_host_reset_event_callback();
    if (device_provisioning_set_event_callback(host_event_callback, nullptr) !=
        ESP_OK) {
        host_event_callback_calls = -1;
    }
}

device_provisioning_event_t device_provisioning_host_last_callback_event(void) {
    return host_callback_event;
}

esp_err_t network_manager_store_credentials(
    const char *ssid,
    const char *password
) {
    ++host_store_credentials_calls;
    if (host_store_credentials_result != ESP_OK) {
        return host_store_credentials_result;
    }
    return static_cast<esp_err_t>(
        config_store_set_string("wifi_ssid", ssid) == ESP_OK &&
                config_store_set_string("wifi_pass", password) == ESP_OK
            ? ESP_OK
            : ESP_FAIL
    );
}

esp_err_t network_manager_reconnect_stored(void) {
    ++host_reconnect_calls;
    return host_reconnect_result;
}

const char *provisioning_payload_security_username(void) {
    return "sprout-provisioning";
}

esp_err_t provisioning_payload_build_setup_qr(
    const char *service_name,
    const char *proof_of_possession,
    char *output,
    size_t output_size
) {
    if (service_name == nullptr || proof_of_possession == nullptr ||
        output == nullptr || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const int written = snprintf(
        output,
        output_size,
        "sprout://setup?service=%s&pop=%s",
        service_name,
        proof_of_possession
    );
    return written > 0 && static_cast<size_t>(written) < output_size
               ? ESP_OK
               : ESP_ERR_INVALID_SIZE;
}

esp_err_t provisioning_payload_build_qr(
    const char *device_id,
    const char *binding_token,
    const char *device_name,
    char *output,
    size_t output_size
) {
    if (device_id == nullptr || binding_token == nullptr ||
        device_name == nullptr || output == nullptr || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const int written = snprintf(
        output,
        output_size,
        "sprout://device?device_id=%s&token=%s&name=%s",
        device_id,
        binding_token,
        device_name
    );
    return written > 0 && static_cast<size_t>(written) < output_size
               ? ESP_OK
               : ESP_ERR_INVALID_SIZE;
}

}  // extern "C"
