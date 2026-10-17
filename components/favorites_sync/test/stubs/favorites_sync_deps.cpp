#include "favorites_sync_host.h"

#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "network_manager.h"
#include "time_sync.h"
#include "transport_security.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

extern "C" {

void favorites_test_store_reset(void);

#define HOST_REQUEST_CAPACITY 32
#define HOST_RESPONSE_COUNT 16

typedef struct {
    char url[512];
    char authorization[192];
    esp_http_client_method_t method;
} host_request_t;

typedef struct esp_http_client {
    host_request_t *request;
    int status_code;
    size_t response_index;
    http_event_handle_cb event_handler;
    void *user_data;
} host_client_t;

static host_request_t host_requests[HOST_REQUEST_CAPACITY];
static size_t host_request_count;
static network_manager_state_t host_network_state =
    NETWORK_MANAGER_STATE_CONNECTED;
static bool host_time_synchronized = true;
static bool host_registered = true;
static char host_session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] =
    "session-1";
static char host_device_id[DEVICE_IDENTIFIER_SIZE] = "device-0001";
static char host_base_url[384] = "https://api.clarkhub.cn";
static esp_err_t host_copy_session_result = ESP_OK;
static esp_err_t host_identity_result = ESP_OK;
static esp_err_t host_base_url_result = ESP_OK;
static esp_err_t host_transport_security_result = TRANSPORT_SECURITY_OK;
static esp_err_t host_perform_result = ESP_OK;
static int host_status_codes[HOST_RESPONSE_COUNT] = {200};
static const char *host_bodies[HOST_RESPONSE_COUNT] = {"{}"};
static size_t host_response_count = 1;
static bool host_client_init_fails;
static bool host_task_create_fails;
static int host_stop_after_delays = -1;
static int host_clear_session_calls;
static int host_task_create_calls;
static int host_task_delete_calls;
static int host_delay_calls;
static int host_transport_security_calls;
static TaskFunction_t host_pending_task;
static bool host_task_pending;

static const char *host_response_body(size_t index) {
    if (host_response_count == 0) {
        return "";
    }
    const size_t bounded = index < host_response_count
        ? index
        : host_response_count - 1;
    return host_bodies[bounded] != nullptr ? host_bodies[bounded] : "";
}

static int host_response_status(size_t index) {
    if (host_response_count == 0) {
        return 200;
    }
    const size_t bounded = index < host_response_count
        ? index
        : host_response_count - 1;
    return host_status_codes[bounded];
}

void favorites_sync_host_reset(void) {
    memset(host_requests, 0, sizeof(host_requests));
    host_request_count = 0;
    host_network_state = NETWORK_MANAGER_STATE_CONNECTED;
    host_time_synchronized = true;
    host_registered = true;
    strcpy(host_session_token, "session-1");
    strcpy(host_device_id, "device-0001");
    strcpy(host_base_url, "https://api.clarkhub.cn");
    host_copy_session_result = ESP_OK;
    host_identity_result = ESP_OK;
    host_base_url_result = ESP_OK;
    host_transport_security_result = TRANSPORT_SECURITY_OK;
    host_perform_result = ESP_OK;
    host_status_codes[0] = 200;
    host_bodies[0] = "{}";
    host_response_count = 1;
    host_client_init_fails = false;
    host_task_create_fails = false;
    host_stop_after_delays = -1;
    host_clear_session_calls = 0;
    host_task_create_calls = 0;
    host_task_delete_calls = 0;
    host_delay_calls = 0;
    host_transport_security_calls = 0;
    host_pending_task = nullptr;
    host_task_pending = false;
}

void favorites_sync_host_reset_store(void) {
    favorites_test_store_reset();
}

void favorites_sync_host_set_network_state(network_manager_state_t state) {
    host_network_state = state;
}

void favorites_sync_host_set_time_synchronized(bool synchronized) {
    host_time_synchronized = synchronized;
}

void favorites_sync_host_set_registered(bool registered) {
    host_registered = registered;
}

void favorites_sync_host_set_session_token(const char *token) {
    if (token == nullptr) {
        host_session_token[0] = '\0';
        return;
    }
    strncpy(host_session_token, token, sizeof(host_session_token) - 1);
    host_session_token[sizeof(host_session_token) - 1] = '\0';
}

void favorites_sync_host_set_device_id(const char *device_id) {
    if (device_id == nullptr) {
        host_device_id[0] = '\0';
        return;
    }
    strncpy(host_device_id, device_id, sizeof(host_device_id) - 1);
    host_device_id[sizeof(host_device_id) - 1] = '\0';
}

void favorites_sync_host_set_base_url(const char *base_url) {
    if (base_url == nullptr) {
        host_base_url[0] = '\0';
        return;
    }
    strncpy(host_base_url, base_url, sizeof(host_base_url) - 1);
    host_base_url[sizeof(host_base_url) - 1] = '\0';
}

void favorites_sync_host_set_copy_session_result(esp_err_t result) {
    host_copy_session_result = result;
}

void favorites_sync_host_set_identity_result(esp_err_t result) {
    host_identity_result = result;
}

void favorites_sync_host_set_base_url_result(esp_err_t result) {
    host_base_url_result = result;
}

void favorites_sync_host_set_transport_security_result(esp_err_t result) {
    host_transport_security_result = result;
}

void favorites_sync_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
) {
    host_response_count = count > HOST_RESPONSE_COUNT
        ? HOST_RESPONSE_COUNT
        : count;
    for (size_t index = 0; index < host_response_count; ++index) {
        host_status_codes[index] = status_codes[index];
        host_bodies[index] = bodies[index];
    }
}

void favorites_sync_host_set_perform_result(esp_err_t result) {
    host_perform_result = result;
}

void favorites_sync_host_set_client_init_fails(bool fails) {
    host_client_init_fails = fails;
}

void favorites_sync_host_set_task_create_fails(bool fails) {
    host_task_create_fails = fails;
}

void favorites_sync_host_set_stop_after_delays(int delay_count) {
    host_stop_after_delays = delay_count;
}

int favorites_sync_host_clear_session_calls(void) {
    return host_clear_session_calls;
}

int favorites_sync_host_transport_security_calls(void) {
    return host_transport_security_calls;
}

int favorites_sync_host_task_create_calls(void) {
    return host_task_create_calls;
}

int favorites_sync_host_task_delete_calls(void) {
    return host_task_delete_calls;
}

int favorites_sync_host_delay_calls(void) {
    return host_delay_calls;
}

bool favorites_sync_host_task_pending(void) {
    return host_task_pending;
}

size_t favorites_sync_host_request_count(void) {
    return host_request_count;
}

const char *favorites_sync_host_request_url(size_t index) {
    return index < host_request_count ? host_requests[index].url : "";
}

const char *favorites_sync_host_request_authorization(size_t index) {
    return index < host_request_count
        ? host_requests[index].authorization
        : "";
}

esp_http_client_method_t favorites_sync_host_request_method(size_t index) {
    return index < host_request_count
        ? host_requests[index].method
        : HTTP_METHOD_GET;
}

void favorites_sync_host_run_pending_task(void) {
    if (host_pending_task != nullptr) {
        host_pending_task(nullptr);
    }
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
    if (host_copy_session_result != ESP_OK) {
        return host_copy_session_result;
    }
    if (output == nullptr || strlen(host_session_token) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    strcpy(output, host_session_token);
    return ESP_OK;
}

esp_err_t device_binding_client_clear_session_token(void) {
    host_clear_session_calls += 1;
    host_session_token[0] = '\0';
    return ESP_OK;
}

esp_err_t device_binding_client_get_platform_base_url(
    char *output,
    size_t output_size
) {
    if (host_base_url_result != ESP_OK) {
        return host_base_url_result;
    }
    if (output == nullptr || strlen(host_base_url) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    strcpy(output, host_base_url);
    return ESP_OK;
}

esp_err_t device_identity_copy(char *output, size_t output_size) {
    if (host_identity_result != ESP_OK) {
        return host_identity_result;
    }
    if (output == nullptr || strlen(host_device_id) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    strcpy(output, host_device_id);
    return ESP_OK;
}

transport_security_error_t transport_security_validate_outbound(
    const char *url,
    transport_security_endpoint_t *endpoint
) {
    (void)url;
    host_transport_security_calls += 1;
    if (endpoint != nullptr) {
        memset(endpoint, 0, sizeof(*endpoint));
    }
    return (transport_security_error_t)host_transport_security_result;
}

int esp_crt_bundle_attach(void *conf) {
    (void)conf;
    return ESP_OK;
}

esp_http_client_handle_t esp_http_client_init(
    const esp_http_client_config_t *config
) {
    if (host_client_init_fails || config == nullptr ||
        host_request_count >= HOST_REQUEST_CAPACITY) {
        return nullptr;
    }
    host_client_t *client = (host_client_t *)calloc(1, sizeof(*client));
    if (client == nullptr) {
        return nullptr;
    }
    host_request_t *request = &host_requests[host_request_count];
    memset(request, 0, sizeof(*request));
    if (config->url != nullptr) {
        strncpy(request->url, config->url, sizeof(request->url) - 1);
    }
    request->method = config->method;
    client->request = request;
    client->status_code = host_response_status(host_request_count);
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
    if (strcmp(key, "Authorization") == 0) {
        strncpy(
            client->request->authorization,
            value,
            sizeof(client->request->authorization) - 1
        );
    }
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
        const char *body = host_response_body(client->response_index);
        esp_http_client_event_t event = {};
        event.event_id = HTTP_EVENT_ON_DATA;
        event.client = client;
        event.data = (void *)body;
        event.data_len = (int)strlen(body);
        event.user_data = client->user_data;
        client->event_handler(&event);
    }
    return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    return client == nullptr ? 0 : client->status_code;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    free(client);
    return ESP_OK;
}

BaseType_t xTaskCreate(
    TaskFunction_t task,
    const char *name,
    uint32_t stack_size,
    void *argument,
    UBaseType_t priority,
    TaskHandle_t *task_handle
) {
    (void)name;
    (void)stack_size;
    (void)argument;
    (void)priority;
    host_task_create_calls += 1;
    if (host_task_create_fails) {
        return pdFAIL;
    }
    if (task_handle != nullptr) {
        *task_handle = (TaskHandle_t)task;
    }
    host_pending_task = task;
    host_task_pending = true;
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
    (void)ticks;
    host_delay_calls += 1;
    if (host_stop_after_delays >= 0 &&
        host_delay_calls > host_stop_after_delays) {
        favorites_sync_host_set_stop(true);
    }
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
    host_task_delete_calls += 1;
    host_task_pending = false;
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

}  // extern "C"
