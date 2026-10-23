#include "emotion_report_host.h"

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

#define HOST_REQUEST_CAPACITY 32
#define HOST_REQUEST_BODY_SIZE 512

typedef struct {
    char url[512];
    char authorization[192];
    char body[HOST_REQUEST_BODY_SIZE];
    esp_http_client_method_t method;
} host_request_t;

typedef struct esp_http_client {
    host_request_t *request;
    int status_code;
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
static esp_err_t host_transport_security_result = TRANSPORT_SECURITY_OK;
static esp_err_t host_perform_result = ESP_OK;
static int host_status_code = 201;
static bool host_client_init_fails;
static bool host_task_create_fails;
static int host_clear_session_calls;
static int host_task_create_calls;
static int host_task_delete_calls;
static int host_delay_calls;
static int64_t host_now_epoch_seconds = 1700000000;
static TaskFunction_t host_pending_task;
static bool host_task_pending;

static void host_copy(char *destination, size_t size, const char *value) {
    if (value == nullptr) {
        destination[0] = '\0';
        return;
    }
    strncpy(destination, value, size - 1);
    destination[size - 1] = '\0';
}

void emotion_report_host_reset(void) {
    memset(host_requests, 0, sizeof(host_requests));
    host_request_count = 0;
    host_network_state = NETWORK_MANAGER_STATE_CONNECTED;
    host_time_synchronized = true;
    host_registered = true;
    host_copy(host_session_token, sizeof(host_session_token), "session-1");
    host_copy(host_device_id, sizeof(host_device_id), "device-0001");
    host_copy(host_base_url, sizeof(host_base_url), "https://api.clarkhub.cn");
    host_copy_session_result = ESP_OK;
    host_transport_security_result = TRANSPORT_SECURITY_OK;
    host_perform_result = ESP_OK;
    host_status_code = 201;
    host_client_init_fails = false;
    host_task_create_fails = false;
    host_clear_session_calls = 0;
    host_task_create_calls = 0;
    host_task_delete_calls = 0;
    host_delay_calls = 0;
    host_now_epoch_seconds = 1700000000;
    host_pending_task = nullptr;
    host_task_pending = false;
}

void emotion_report_host_set_network_state(network_manager_state_t state) {
    host_network_state = state;
}

void emotion_report_host_set_time_synchronized(bool synchronized) {
    host_time_synchronized = synchronized;
}

void emotion_report_host_set_registered(bool registered) {
    host_registered = registered;
}

void emotion_report_host_set_session_token(const char *token) {
    host_copy(host_session_token, sizeof(host_session_token), token);
}

void emotion_report_host_set_device_id(const char *device_id) {
    host_copy(host_device_id, sizeof(host_device_id), device_id);
}

void emotion_report_host_set_base_url(const char *base_url) {
    host_copy(host_base_url, sizeof(host_base_url), base_url);
}

void emotion_report_host_set_copy_session_result(esp_err_t result) {
    host_copy_session_result = result;
}

void emotion_report_host_set_transport_security_result(esp_err_t result) {
    host_transport_security_result = result;
}

void emotion_report_host_set_perform_result(esp_err_t result) {
    host_perform_result = result;
}

void emotion_report_host_set_response_status(int status_code) {
    host_status_code = status_code;
}

void emotion_report_host_set_client_init_fails(bool fails) {
    host_client_init_fails = fails;
}

void emotion_report_host_set_task_create_fails(bool fails) {
    host_task_create_fails = fails;
}

void emotion_report_host_set_now_epoch_seconds(int64_t now) {
    host_now_epoch_seconds = now;
}

int64_t emotion_report_host_now_epoch_seconds(void) {
    return host_now_epoch_seconds;
}

int emotion_report_host_clear_session_calls(void) {
    return host_clear_session_calls;
}

int emotion_report_host_task_create_calls(void) {
    return host_task_create_calls;
}

int emotion_report_host_task_delete_calls(void) {
    return host_task_delete_calls;
}

int emotion_report_host_delay_calls(void) {
    return host_delay_calls;
}

size_t emotion_report_host_request_count(void) {
    return host_request_count;
}

const char *emotion_report_host_request_url(size_t index) {
    return index < host_request_count ? host_requests[index].url : "";
}

const char *emotion_report_host_request_authorization(size_t index) {
    return index < host_request_count
        ? host_requests[index].authorization
        : "";
}

const char *emotion_report_host_request_body(size_t index) {
    return index < host_request_count ? host_requests[index].body : "";
}

esp_http_client_method_t emotion_report_host_request_method(size_t index) {
    return index < host_request_count
        ? host_requests[index].method
        : HTTP_METHOD_GET;
}

bool emotion_report_host_task_pending(void) {
    return host_task_pending;
}

network_manager_state_t network_manager_get_state(void) {
    return host_network_state;
}

bool time_sync_is_synchronized(void) {
    return host_time_synchronized;
}

time_t time(time_t *timer) {
    const time_t now = (time_t)host_now_epoch_seconds;
    if (timer != nullptr) {
        *timer = now;
    }
    return now;
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
    if (output == nullptr || strlen(host_base_url) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    strcpy(output, host_base_url);
    return ESP_OK;
}

esp_err_t device_identity_copy(char *output, size_t output_size) {
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
    host_copy(request->url, sizeof(request->url), config->url);
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
    if (strcmp(key, "Authorization") == 0) {
        host_copy(
            client->request->authorization,
            sizeof(client->request->authorization),
            value
        );
    }
    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(
    esp_http_client_handle_t client,
    const char *data,
    int len
) {
    if (client == nullptr || data == nullptr || len <= 0 ||
        (size_t)len >= HOST_REQUEST_BODY_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(client->request->body, data, (size_t)len);
    client->request->body[len] = '\0';
    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
    if (client == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return host_perform_result;
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
        default:
            return "ESP_ERR_UNKNOWN";
    }
}

}  // extern "C"
