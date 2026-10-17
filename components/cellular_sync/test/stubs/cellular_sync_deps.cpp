#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "cellular_4g.h"
#include "config_store.h"
#include "device_binding_client.h"
#include "device_identity.h"
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
    char body[1024];
    esp_http_client_method_t method;
} host_request_t;

typedef struct esp_http_client {
    host_request_t *request;
    int status_code;
    size_t response_index;
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
static int host_status_codes[8] = {200};
static const char *host_bodies[8] = {"{}"};
static size_t host_response_count = 1;
static esp_err_t host_perform_result = ESP_OK;
static transport_security_error_t host_transport_result =
    TRANSPORT_SECURITY_OK;
static esp_err_t host_store_result = ESP_OK;
static int host_clear_calls;
static int host_transport_calls;
static int host_store_calls;
static bool host_has_stored_policy;
static uint32_t host_stored_revision;

void cellular_sync_host_reset(void) {
    std::memset(host_requests, 0, sizeof(host_requests));
    host_request_count = 0;
    host_network_state = NETWORK_MANAGER_STATE_CONNECTED;
    host_time_synchronized = true;
    host_registered = true;
    std::strcpy(host_session_token, "session-1");
    std::strcpy(host_device_id, "device-0001");
    std::strcpy(host_base_url, "https://api.clarkhub.cn");
    host_status_codes[0] = 200;
    host_bodies[0] = "{}";
    host_response_count = 1;
    host_perform_result = ESP_OK;
    host_transport_result = TRANSPORT_SECURITY_OK;
    host_store_result = ESP_OK;
    host_clear_calls = 0;
    host_transport_calls = 0;
    host_store_calls = 0;
    host_has_stored_policy = false;
    host_stored_revision = 0;
}

void cellular_sync_host_set_network_state(network_manager_state_t state) {
    host_network_state = state;
}

void cellular_sync_host_set_time_synchronized(bool synchronized) {
    host_time_synchronized = synchronized;
}

void cellular_sync_host_set_registered(bool registered) {
    host_registered = registered;
}

void cellular_sync_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
) {
    const size_t bounded = count > 8 ? 8 : count;
    host_response_count = bounded;
    for (size_t index = 0; index < bounded; ++index) {
        host_status_codes[index] = status_codes[index];
        host_bodies[index] = bodies[index] != nullptr ? bodies[index] : "";
    }
}

void cellular_sync_host_set_perform_result(esp_err_t result) {
    host_perform_result = result;
}

void cellular_sync_host_set_transport_result(esp_err_t result) {
    host_transport_result = (transport_security_error_t)result;
}

void cellular_sync_host_set_store_result(esp_err_t result) {
    host_store_result = result;
}

size_t cellular_sync_host_request_count(void) {
    return host_request_count;
}

const char *cellular_sync_host_request_url(size_t index) {
    return index < host_request_count ? host_requests[index].url : "";
}

const char *cellular_sync_host_request_authorization(size_t index) {
    return index < host_request_count
        ? host_requests[index].authorization
        : "";
}

const char *cellular_sync_host_request_body(size_t index) {
    return index < host_request_count ? host_requests[index].body : "";
}

esp_http_client_method_t cellular_sync_host_request_method(size_t index) {
    return index < host_request_count
        ? host_requests[index].method
        : HTTP_METHOD_GET;
}

int cellular_sync_host_clear_session_calls(void) {
    return host_clear_calls;
}

int cellular_sync_host_transport_calls(void) {
    return host_transport_calls;
}

int cellular_sync_host_store_calls(void) {
    return host_store_calls;
}

bool cellular_sync_host_has_stored_policy(void) {
    return host_has_stored_policy;
}

uint32_t cellular_sync_host_stored_revision(void) {
    return host_stored_revision;
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
    host_session_token[0] = '\0';
    return ESP_OK;
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
    if (value == nullptr || size < sizeof(cellular_policy_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    host_has_stored_policy = true;
    std::memcpy(
        &host_stored_revision,
        (const unsigned char *)value + offsetof(cellular_policy_t, revision),
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
    client->status_code = host_response_count == 0
        ? 200
        : host_status_codes[
            host_request_count < host_response_count
                ? host_request_count
                : host_response_count - 1
        ];
    client->response_index = host_request_count;
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
    if (client == nullptr || data == nullptr || len < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t bounded = (size_t)len < sizeof(client->request->body) - 1
        ? (size_t)len
        : sizeof(client->request->body) - 1;
    std::memcpy(client->request->body, data, bounded);
    client->request->body[bounded] = '\0';
    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
    if (client == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_perform_result != ESP_OK) {
        return host_perform_result;
    }
    const size_t bounded = host_response_count == 0
        ? 0
        : (client->response_index < host_response_count
            ? client->response_index
            : host_response_count - 1);
    const char *body = host_response_count == 0
        ? ""
        : (host_bodies[bounded] != nullptr ? host_bodies[bounded] : "");
    if (client->event_handler != nullptr) {
        esp_http_client_event_t event = {};
        event.event_id = HTTP_EVENT_ON_DATA;
        event.client = client;
        event.data = (void *)body;
        event.data_len = (int)std::strlen(body);
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
