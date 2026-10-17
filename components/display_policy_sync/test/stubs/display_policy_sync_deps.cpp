#include <cstdlib>
#include <cstring>
#include <cstddef>

#include "config_store.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "display_policy.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "time_sync.h"
#include "transport_security.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char url[512];
    char authorization[192];
    esp_http_client_method_t method;
} host_request_t;

typedef struct esp_http_client {
    host_request_t *request;
    int status_code;
    http_event_handle_cb event_handler;
    void *user_data;
} host_client_t;

static host_request_t host_requests[8];
static size_t host_request_count;
static network_manager_state_t host_network_state =
    NETWORK_MANAGER_STATE_CONNECTED;
static bool host_time_synchronized = true;
static bool host_registered = true;
static char host_session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] =
    "session-1";
static char host_device_id[DEVICE_IDENTIFIER_SIZE] = "device-0001";
static char host_base_url[384] = "https://api.clarkhub.cn";
static int host_status_code = 200;
static const char *host_body = "{}";
static esp_err_t host_perform_result = ESP_OK;
static esp_err_t host_store_result = ESP_OK;
static esp_err_t host_clear_result = ESP_OK;
static int host_clear_calls;
static int host_store_calls;
static bool host_has_stored_policy;
static uint32_t host_stored_revision;
static transport_security_error_t host_transport_result =
    TRANSPORT_SECURITY_OK;
static int host_transport_calls;

void display_policy_sync_host_reset(void) {
    std::memset(host_requests, 0, sizeof(host_requests));
    host_request_count = 0;
    host_network_state = NETWORK_MANAGER_STATE_CONNECTED;
    host_time_synchronized = true;
    host_registered = true;
    std::strcpy(host_session_token, "session-1");
    std::strcpy(host_device_id, "device-0001");
    std::strcpy(host_base_url, "https://api.clarkhub.cn");
    host_status_code = 200;
    host_body = "{}";
    host_perform_result = ESP_OK;
    host_store_result = ESP_OK;
    host_clear_result = ESP_OK;
    host_clear_calls = 0;
    host_store_calls = 0;
    host_has_stored_policy = false;
    host_stored_revision = 0;
    host_transport_result = TRANSPORT_SECURITY_OK;
    host_transport_calls = 0;
}

void display_policy_sync_host_set_network_state(network_manager_state_t state) {
    host_network_state = state;
}

void display_policy_sync_host_set_time_synchronized(bool synchronized) {
    host_time_synchronized = synchronized;
}

void display_policy_sync_host_set_registered(bool registered) {
    host_registered = registered;
}

void display_policy_sync_host_set_body(int status_code, const char *body) {
    host_status_code = status_code;
    host_body = body != nullptr ? body : "";
}

void display_policy_sync_host_set_perform_result(esp_err_t result) {
    host_perform_result = result;
}

void display_policy_sync_host_set_store_result(esp_err_t result) {
    host_store_result = result;
}

void display_policy_sync_host_set_transport_result(esp_err_t result) {
    host_transport_result = (transport_security_error_t)result;
}

size_t display_policy_sync_host_request_count(void) {
    return host_request_count;
}

const char *display_policy_sync_host_request_url(size_t index) {
    return index < host_request_count ? host_requests[index].url : "";
}

esp_http_client_method_t display_policy_sync_host_request_method(
    size_t index
) {
    return index < host_request_count
        ? host_requests[index].method
        : HTTP_METHOD_GET;
}

const char *display_policy_sync_host_request_authorization(size_t index) {
    return index < host_request_count
        ? host_requests[index].authorization
        : "";
}

int display_policy_sync_host_clear_session_calls(void) {
    return host_clear_calls;
}

int display_policy_sync_host_store_calls(void) {
    return host_store_calls;
}

bool display_policy_sync_host_has_stored_policy(void) {
    return host_has_stored_policy;
}

uint32_t display_policy_sync_host_stored_revision(void) {
    return host_stored_revision;
}

int display_policy_sync_host_transport_calls(void) {
    return host_transport_calls;
}

transport_security_error_t transport_security_validate_outbound(
    const char *url,
    transport_security_endpoint_t *endpoint
) {
    (void)url;
    host_transport_calls += 1;
    if (endpoint != nullptr) {
        std::memset(endpoint, 0, sizeof(*endpoint));
    }
    return host_transport_result;
}

network_manager_state_t network_manager_get_state(void) {
    return host_network_state;
}

bool time_sync_is_synchronized(void) {
    return host_time_synchronized;
}

bool device_binding_client_is_registered(void) {
    return host_registered;
}

esp_err_t device_binding_client_copy_session_token(
    char *output,
    size_t output_size
) {
    if (output == nullptr ||
        std::strlen(host_session_token) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::strcpy(output, host_session_token);
    return ESP_OK;
}

esp_err_t device_binding_client_clear_session_token(void) {
    host_clear_calls += 1;
    if (host_clear_result == ESP_OK) {
        host_session_token[0] = '\0';
    }
    return host_clear_result;
}

esp_err_t device_binding_client_get_platform_base_url(
    char *output,
    size_t output_size
) {
    if (output == nullptr || std::strlen(host_base_url) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::strcpy(output, host_base_url);
    return ESP_OK;
}

esp_err_t device_identity_copy(char *output, size_t output_size) {
    if (output == nullptr || std::strlen(host_device_id) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::strcpy(output, host_device_id);
    return ESP_OK;
}

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *size
) {
    (void)key;
    (void)output;
    (void)size;
    return CONFIG_STORE_ERR_NOT_FOUND;
}

esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t size
) {
    (void)key;
    host_store_calls += 1;
    if (host_store_result != ESP_OK) {
        return host_store_result;
    }
    if (value == nullptr || size < sizeof(uint32_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    host_has_stored_policy = true;
    std::memcpy(
        &host_stored_revision,
        (const unsigned char *)value + offsetof(display_policy_t, revision),
        sizeof(host_stored_revision)
    );
    return ESP_OK;
}

int esp_crt_bundle_attach(void *conf) {
    (void)conf;
    return ESP_OK;
}

esp_http_client_handle_t esp_http_client_init(
    const esp_http_client_config_t *config
) {
    if (config == nullptr || host_request_count >= 8) {
        return nullptr;
    }
    host_client_t *client =
        (host_client_t *)std::calloc(1, sizeof(host_client_t));
    if (client == nullptr) {
        return nullptr;
    }
    host_request_t *request = &host_requests[host_request_count];
    std::strncpy(request->url, config->url, sizeof(request->url) - 1);
    request->method = config->method;
    client->request = request;
    client->status_code = host_status_code;
    client->event_handler = config->event_handler;
    client->user_data = config->user_data;
    host_request_count += 1;
    return (esp_http_client_handle_t)client;
}

esp_err_t esp_http_client_set_header(
    esp_http_client_handle_t client,
    const char *key,
    const char *value
) {
    if (client == nullptr || key == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strcmp(key, "Authorization") == 0) {
        std::strncpy(
            client->request->authorization,
            value,
            sizeof(client->request->authorization) - 1
        );
    }
    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(
    esp_http_client_handle_t client,
    const char *data,
    int len
) {
    (void)client;
    (void)data;
    (void)len;
    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
    if (client == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_perform_result != ESP_OK) {
        return host_perform_result;
    }
    if (client->event_handler != nullptr) {
        esp_http_client_event_t event = {};
        event.event_id = HTTP_EVENT_ON_DATA;
        event.client = client;
        event.data = (void *)host_body;
        event.data_len = (int)std::strlen(host_body);
        event.user_data = client->user_data;
        (void)client->event_handler(&event);
    }
    return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    return client == nullptr ? 0 : client->status_code;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    std::free(client);
    return ESP_OK;
}

const char *esp_err_to_name(esp_err_t error) {
    (void)error;
    return "esp_error";
}

BaseType_t xTaskCreate(
    TaskFunction_t task,
    const char *name,
    uint32_t stack_size,
    void *argument,
    UBaseType_t priority,
    TaskHandle_t *task_handle
) {
    (void)task;
    (void)name;
    (void)stack_size;
    (void)argument;
    (void)priority;
    if (task_handle != nullptr) {
        *task_handle = nullptr;
    }
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
    (void)ticks;
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
}

#ifdef __cplusplus
}
#endif
