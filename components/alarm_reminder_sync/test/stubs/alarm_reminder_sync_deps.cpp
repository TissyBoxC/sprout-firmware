#include "alarm_reminder_sync_host.h"

#include "config_store.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_http_client.h"
#include "network_manager.h"
#include "time_sync.h"
#include "transport_security.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

extern "C" {

#define HOST_CONFIG_CAPACITY 32
#define HOST_BLOB_CAPACITY 16384
#define HOST_REQUEST_CAPACITY 32
#define HOST_REQUEST_BODY_CAPACITY 2048
#define HOST_RESPONSE_COUNT 16

typedef struct {
    char key[CONFIG_STORE_KEY_MAX_SIZE + 1];
    unsigned char value[HOST_BLOB_CAPACITY];
    size_t value_size;
    bool is_blob;
    bool used;
} host_config_entry_t;

typedef struct {
    char url[512];
    char authorization[160];
    char body[HOST_REQUEST_BODY_CAPACITY];
    esp_http_client_method_t method;
} host_request_t;

typedef struct esp_http_client {
    host_request_t *request;
    int status_code;
    size_t response_index;
    http_event_handle_cb event_handler;
    void *user_data;
} host_client_t;

static host_config_entry_t host_config[HOST_CONFIG_CAPACITY];
static host_request_t host_requests[HOST_REQUEST_CAPACITY];
static size_t host_request_count;
static network_manager_state_t host_network_state =
    NETWORK_MANAGER_STATE_CONNECTED;
static bool host_time_synchronized = true;
static bool host_registered = true;
static bool host_bound = true;
static char host_session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = "session-1";
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
static int host_semaphore_create_calls;
static int host_semaphore_delete_calls;
static int host_transport_security_calls;
static TaskFunction_t host_pending_task;
static bool host_task_pending;

static host_config_entry_t *host_find_config(const char *key) {
    for (size_t index = 0; index < HOST_CONFIG_CAPACITY; ++index) {
        if (host_config[index].used &&
            strcmp(host_config[index].key, key) == 0) {
            return &host_config[index];
        }
    }
    return nullptr;
}

static host_config_entry_t *host_allocate_config(const char *key) {
    host_config_entry_t *entry = host_find_config(key);
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
            return &host_config[index];
        }
    }
    return nullptr;
}

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

void alarm_reminder_sync_host_reset(void) {
    memset(host_config, 0, sizeof(host_config));
    memset(host_requests, 0, sizeof(host_requests));
    host_request_count = 0;
    host_network_state = NETWORK_MANAGER_STATE_CONNECTED;
    host_time_synchronized = true;
    host_registered = true;
    host_bound = true;
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
    host_semaphore_create_calls = 0;
    host_semaphore_delete_calls = 0;
    host_transport_security_calls = 0;
    host_pending_task = nullptr;
    host_task_pending = false;
}

void alarm_reminder_sync_host_set_network_state(
    network_manager_state_t state
) {
    host_network_state = state;
}

void alarm_reminder_sync_host_set_time_synchronized(bool synchronized) {
    host_time_synchronized = synchronized;
}

void alarm_reminder_sync_host_set_registered(bool registered) {
    host_registered = registered;
}

void alarm_reminder_sync_host_set_bound(bool bound) {
    host_bound = bound;
}

void alarm_reminder_sync_host_set_session_token(const char *token) {
    if (token == nullptr) {
        host_session_token[0] = '\0';
        return;
    }
    strncpy(host_session_token, token, sizeof(host_session_token) - 1);
    host_session_token[sizeof(host_session_token) - 1] = '\0';
}

void alarm_reminder_sync_host_set_device_id(const char *device_id) {
    if (device_id == nullptr) {
        host_device_id[0] = '\0';
        return;
    }
    strncpy(host_device_id, device_id, sizeof(host_device_id) - 1);
    host_device_id[sizeof(host_device_id) - 1] = '\0';
}

void alarm_reminder_sync_host_set_base_url(const char *base_url) {
    if (base_url == nullptr) {
        host_base_url[0] = '\0';
        return;
    }
    strncpy(host_base_url, base_url, sizeof(host_base_url) - 1);
    host_base_url[sizeof(host_base_url) - 1] = '\0';
}

void alarm_reminder_sync_host_set_copy_session_result(esp_err_t result) {
    host_copy_session_result = result;
}

void alarm_reminder_sync_host_set_identity_result(esp_err_t result) {
    host_identity_result = result;
}

void alarm_reminder_sync_host_set_base_url_result(esp_err_t result) {
    host_base_url_result = result;
}

void alarm_reminder_sync_host_set_transport_security_result(esp_err_t result) {
    host_transport_security_result = result;
}

void alarm_reminder_sync_host_set_http_sequence(
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

void alarm_reminder_sync_host_set_perform_result(esp_err_t result) {
    host_perform_result = result;
}

void alarm_reminder_sync_host_set_client_init_fails(bool fails) {
    host_client_init_fails = fails;
}

void alarm_reminder_sync_host_set_task_create_fails(bool fails) {
    host_task_create_fails = fails;
}

void alarm_reminder_sync_host_set_stop_after_delays(int delay_count) {
    host_stop_after_delays = delay_count;
}

int alarm_reminder_sync_host_perform_calls(void) {
    return (int)host_request_count;
}

int alarm_reminder_sync_host_client_init_calls(void) {
    return (int)host_request_count;
}

int alarm_reminder_sync_host_cleanup_calls(void) {
    return (int)host_request_count;
}

int alarm_reminder_sync_host_clear_session_calls(void) {
    return host_clear_session_calls;
}

int alarm_reminder_sync_host_transport_security_calls(void) {
    return host_transport_security_calls;
}

int alarm_reminder_sync_host_task_create_calls(void) {
    return host_task_create_calls;
}

int alarm_reminder_sync_host_task_delete_calls(void) {
    return host_task_delete_calls;
}

int alarm_reminder_sync_host_delay_calls(void) {
    return host_delay_calls;
}

int alarm_reminder_sync_host_semaphore_create_calls(void) {
    return host_semaphore_create_calls;
}

int alarm_reminder_sync_host_semaphore_delete_calls(void) {
    return host_semaphore_delete_calls;
}

bool alarm_reminder_sync_host_task_pending(void) {
    return host_task_pending;
}

size_t alarm_reminder_sync_host_request_count(void) {
    return host_request_count;
}

const char *alarm_reminder_sync_host_request_url(size_t index) {
    return index < host_request_count ? host_requests[index].url : "";
}

const char *alarm_reminder_sync_host_request_body(size_t index) {
    return index < host_request_count ? host_requests[index].body : "";
}

const char *alarm_reminder_sync_host_request_authorization(size_t index) {
    return index < host_request_count
        ? host_requests[index].authorization
        : "";
}

esp_http_client_method_t alarm_reminder_sync_host_request_method(
    size_t index
) {
    return index < host_request_count
        ? host_requests[index].method
        : HTTP_METHOD_GET;
}

void alarm_reminder_sync_host_run_pending_task(void) {
    if (host_pending_task != nullptr) {
        host_pending_task(nullptr);
    }
}

esp_err_t config_store_init(void) {
    return ESP_OK;
}

bool config_store_is_ready(void) {
    return true;
}

esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t value_size
) {
    if (key == nullptr || value == nullptr || value_size == 0 ||
        value_size > HOST_BLOB_CAPACITY) {
        return ESP_ERR_INVALID_ARG;
    }
    host_config_entry_t *entry = host_allocate_config(key);
    if (entry == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(entry->value, value, value_size);
    entry->value_size = value_size;
    entry->is_blob = true;
    return ESP_OK;
}

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *value_size_in_out
) {
    if (key == nullptr || output == nullptr || value_size_in_out == nullptr ||
        *value_size_in_out == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    host_config_entry_t *entry = host_find_config(key);
    if (entry == nullptr || !entry->is_blob) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (*value_size_in_out < entry->value_size) {
        *value_size_in_out = entry->value_size;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, entry->value, entry->value_size);
    *value_size_in_out = entry->value_size;
    return ESP_OK;
}

esp_err_t config_store_set_string(const char *key, const char *value) {
    if (key == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strlen(value);
    if (length > CONFIG_STORE_VALUE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    host_config_entry_t *entry = host_allocate_config(key);
    if (entry == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(entry->value, value, length + 1);
    entry->value_size = length;
    entry->is_blob = false;
    return ESP_OK;
}

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    if (key == nullptr || output == nullptr || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    host_config_entry_t *entry = host_find_config(key);
    if (entry == nullptr || entry->is_blob) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (entry->value_size + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, entry->value, entry->value_size + 1);
    return ESP_OK;
}

esp_err_t config_store_has_key(const char *key, bool *has_key_out) {
    if (key == nullptr || has_key_out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *has_key_out = host_find_config(key) != nullptr;
    return ESP_OK;
}

esp_err_t config_store_erase_key(const char *key) {
    if (key == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    host_config_entry_t *entry = host_find_config(key);
    if (entry != nullptr) {
        memset(entry, 0, sizeof(*entry));
    }
    return ESP_OK;
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

bool device_binding_client_is_bound(void) {
    return host_bound;
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
    host_transport_security_calls += 1;
    (void)url;
    if (endpoint != nullptr) {
        memset(endpoint, 0, sizeof(*endpoint));
    }
    return (transport_security_error_t)host_transport_security_result;
}

esp_err_t esp_crt_bundle_attach(void *conf) {
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

esp_err_t esp_http_client_set_post_field(
    esp_http_client_handle_t client,
    const char *data,
    int length
) {
    if (client == nullptr || data == nullptr || length < 0 ||
        (size_t)length >= sizeof(client->request->body)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(client->request->body, data, (size_t)length);
    client->request->body[length] = '\0';
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
    if (client == nullptr) {
        return 0;
    }
    return client->status_code;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    free(client);
    return ESP_OK;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    host_semaphore_create_calls += 1;
    return (SemaphoreHandle_t)malloc(1);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    (void)semaphore;
    (void)ticks;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    host_semaphore_delete_calls += 1;
    free(semaphore);
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
        alarm_reminder_sync_host_set_stop(true);
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
