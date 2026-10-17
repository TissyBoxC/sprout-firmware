#include "ui_text_sync_host.h"

#include <cstdlib>
#include <cstring>

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
#include "ui_text.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_REQUEST_CAPACITY 8
#define HOST_RESPONSE_CAPACITY 8

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
static esp_err_t host_perform_result = ESP_OK;
static esp_err_t host_transport_security_result = TRANSPORT_SECURITY_OK;
static esp_err_t host_store_string_result = ESP_OK;
static esp_err_t host_store_blob_result = ESP_OK;
static int host_status_codes[HOST_RESPONSE_CAPACITY] = {200};
static const char *host_bodies[HOST_RESPONSE_CAPACITY] = {"{}"};
static size_t host_response_count = 1;
static bool host_client_init_fails;
static int host_clear_session_calls;
static int host_transport_security_calls;
static int host_set_string_calls;
static int host_set_blob_calls;
static char host_cursor[40] = "0";
static char host_stored_package[512] = "{\"package_id\":\"\",\"entries\":[]}";
static ui_text_package_t host_active_package;
static bool host_has_active_package;
static esp_err_t host_apply_result = ESP_OK;
static esp_err_t host_withdraw_result = ESP_OK;
static int host_apply_calls;
static int host_withdraw_calls;
static char host_last_applied_package[UI_TEXT_PACKAGE_ID_SIZE];
static char host_last_withdrawn_package[UI_TEXT_PACKAGE_ID_SIZE];

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

void ui_text_sync_host_reset(void) {
    std::memset(host_requests, 0, sizeof(host_requests));
    host_request_count = 0;
    host_network_state = NETWORK_MANAGER_STATE_CONNECTED;
    host_time_synchronized = true;
    host_registered = true;
    std::strcpy(host_session_token, "session-1");
    std::strcpy(host_device_id, "device-0001");
    std::strcpy(host_base_url, "https://api.clarkhub.cn");
    host_copy_session_result = ESP_OK;
    host_identity_result = ESP_OK;
    host_base_url_result = ESP_OK;
    host_perform_result = ESP_OK;
    host_transport_security_result = TRANSPORT_SECURITY_OK;
    host_store_string_result = ESP_OK;
    host_store_blob_result = ESP_OK;
    host_status_codes[0] = 200;
    host_bodies[0] = "{}";
    host_response_count = 1;
    host_client_init_fails = false;
    host_clear_session_calls = 0;
    host_transport_security_calls = 0;
    host_set_string_calls = 0;
    host_set_blob_calls = 0;
    std::strcpy(host_cursor, "0");
    std::memset(&host_active_package, 0, sizeof(host_active_package));
    host_has_active_package = false;
    host_apply_result = ESP_OK;
    host_withdraw_result = ESP_OK;
    host_apply_calls = 0;
    host_withdraw_calls = 0;
    host_last_applied_package[0] = '\0';
    host_last_withdrawn_package[0] = '\0';
}

void ui_text_sync_host_reset_store(void) {
    host_set_string_calls = 0;
    host_set_blob_calls = 0;
    std::strcpy(host_cursor, "0");
    std::strcpy(
        host_stored_package,
        "{\"package_id\":\"\",\"entries\":[]}"
    );
    std::memset(&host_active_package, 0, sizeof(host_active_package));
    host_has_active_package = false;
}

void ui_text_sync_host_set_network_state(network_manager_state_t state) {
    host_network_state = state;
}

void ui_text_sync_host_set_time_synchronized(bool synchronized) {
    host_time_synchronized = synchronized;
}

void ui_text_sync_host_set_registered(bool registered) {
    host_registered = registered;
}

void ui_text_sync_host_set_copy_session_result(esp_err_t result) {
    host_copy_session_result = result;
}

void ui_text_sync_host_set_identity_result(esp_err_t result) {
    host_identity_result = result;
}

void ui_text_sync_host_set_base_url_result(esp_err_t result) {
    host_base_url_result = result;
}

void ui_text_sync_host_set_perform_result(esp_err_t result) {
    host_perform_result = result;
}

void ui_text_sync_host_set_client_init_fails(bool fails) {
    host_client_init_fails = fails;
}

void ui_text_sync_host_set_transport_security_result(esp_err_t result) {
    host_transport_security_result = result;
}

void ui_text_sync_host_set_store_string_result(esp_err_t result) {
    host_store_string_result = result;
}

void ui_text_sync_host_set_store_blob_result(esp_err_t result) {
    host_store_blob_result = result;
}

void ui_text_sync_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
) {
    host_response_count = count > HOST_RESPONSE_CAPACITY
        ? HOST_RESPONSE_CAPACITY
        : count;
    for (size_t index = 0; index < host_response_count; ++index) {
        host_status_codes[index] = status_codes[index];
        host_bodies[index] = bodies[index];
    }
}

void ui_text_sync_host_set_active_package(
    const char *package_id,
    const char *language,
    unsigned int package_version
) {
    std::memset(&host_active_package, 0, sizeof(host_active_package));
    std::strncpy(
        host_active_package.package_id,
        package_id,
        sizeof(host_active_package.package_id) - 1
    );
    std::strncpy(
        host_active_package.language,
        language,
        sizeof(host_active_package.language) - 1
    );
    host_active_package.package_version = package_version;
    host_has_active_package = true;
}

void ui_text_sync_host_set_apply_result(esp_err_t result) {
    host_apply_result = result;
}

void ui_text_sync_host_set_withdraw_result(esp_err_t result) {
    host_withdraw_result = result;
}

size_t ui_text_sync_host_request_count(void) {
    return host_request_count;
}

const char *ui_text_sync_host_request_url(size_t index) {
    return index < host_request_count ? host_requests[index].url : "";
}

esp_http_client_method_t ui_text_sync_host_request_method(size_t index) {
    return index < host_request_count
        ? host_requests[index].method
        : HTTP_METHOD_GET;
}

const char *ui_text_sync_host_request_authorization(size_t index) {
    return index < host_request_count
        ? host_requests[index].authorization
        : "";
}

int ui_text_sync_host_clear_session_calls(void) {
    return host_clear_session_calls;
}

int ui_text_sync_host_transport_security_calls(void) {
    return host_transport_security_calls;
}

const char *ui_text_sync_host_cursor(void) {
    return host_cursor;
}

int ui_text_sync_host_set_string_calls(void) {
    return host_set_string_calls;
}

int ui_text_sync_host_set_blob_calls(void) {
    return host_set_blob_calls;
}

int ui_text_sync_host_apply_calls(void) {
    return host_apply_calls;
}

int ui_text_sync_host_withdraw_calls(void) {
    return host_withdraw_calls;
}

const char *ui_text_sync_host_last_applied_package(void) {
    return host_last_applied_package;
}

const char *ui_text_sync_host_last_withdrawn_package(void) {
    return host_last_withdrawn_package;
}

const char *ui_text_sync_host_stored_package(void) {
    return host_stored_package;
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
    if (output == nullptr ||
        std::strlen(host_session_token) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::strcpy(output, host_session_token);
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
    if (output == nullptr || std::strlen(host_base_url) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::strcpy(output, host_base_url);
    return ESP_OK;
}

esp_err_t device_identity_copy(char *output, size_t output_size) {
    if (host_identity_result != ESP_OK) {
        return host_identity_result;
    }
    if (output == nullptr || std::strlen(host_device_id) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::strcpy(output, host_device_id);
    return ESP_OK;
}

esp_err_t transport_security_validate_outbound(
    const char *url,
    transport_security_endpoint_t *endpoint
) {
    (void)url;
    host_transport_security_calls += 1;
    if (endpoint != nullptr) {
        std::memset(endpoint, 0, sizeof(*endpoint));
    }
    return host_transport_security_result;
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
    host_client_t *client =
        (host_client_t *)std::calloc(1, sizeof(host_client_t));
    if (client == nullptr) {
        return nullptr;
    }
    host_request_t *request = &host_requests[host_request_count];
    std::memset(request, 0, sizeof(*request));
    if (config->url != nullptr) {
        std::strncpy(request->url, config->url, sizeof(request->url) - 1);
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
        const char *body = host_response_body(client->response_index);
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

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    if (key == nullptr || output == nullptr || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strcmp(key, "uitext_rev") == 0) {
        if (std::strlen(host_cursor) + 1 > output_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        std::strcpy(output, host_cursor);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t config_store_set_string(const char *key, const char *value) {
    host_set_string_calls += 1;
    if (host_store_string_result != ESP_OK) {
        return host_store_string_result;
    }
    if (key == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strcmp(key, "uitext_rev") == 0) {
        std::strncpy(host_cursor, value, sizeof(host_cursor) - 1);
        host_cursor[sizeof(host_cursor) - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t size
) {
    host_set_blob_calls += 1;
    if (host_store_blob_result != ESP_OK) {
        return host_store_blob_result;
    }
    if (key == nullptr || value == nullptr || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const ui_text_package_t *package = (const ui_text_package_t *)value;
    if (size >= sizeof(*package)) {
        std::strncpy(
            host_stored_package,
            package->package_id,
            sizeof(host_stored_package) - 1
        );
    }
    return ESP_OK;
}

bool ui_text_is_ready(void) {
    return true;
}

esp_err_t ui_text_active_package(ui_text_package_t *package_out) {
    if (package_out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!host_has_active_package) {
        return ESP_ERR_NOT_FOUND;
    }
    *package_out = host_active_package;
    return ESP_OK;
}

esp_err_t ui_text_apply_package(
    const ui_text_package_t *package,
    const ui_text_entry_t *entries,
    size_t entry_count
) {
    host_apply_calls += 1;
    if (host_apply_result != ESP_OK) {
        return host_apply_result;
    }
    if (package == nullptr || entries == nullptr || entry_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    host_active_package = *package;
    host_has_active_package = true;
    std::strncpy(
        host_last_applied_package,
        package->package_id,
        sizeof(host_last_applied_package) - 1
    );
    return ESP_OK;
}

esp_err_t ui_text_withdraw_package(const char *package_id) {
    host_withdraw_calls += 1;
    if (host_withdraw_result != ESP_OK) {
        return host_withdraw_result;
    }
    if (package_id == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    std::strncpy(
        host_last_withdrawn_package,
        package_id,
        sizeof(host_last_withdrawn_package) - 1
    );
    host_has_active_package = false;
    std::memset(&host_active_package, 0, sizeof(host_active_package));
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

#ifdef __cplusplus
}
#endif
