#include "parent_policy_host.h"

#include "cloud_auth.h"
#include "config_store.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "parent_policy.h"
#include "transport_security.h"
#include "volume_control.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C" {

void parent_policy_test_store_reset(void);
void parent_policy_test_store_set_ready(bool ready);
bool parent_policy_test_store_has_key(const char *key);
const char *parent_policy_test_store_get(const char *key);
bool parent_policy_test_store_set(const char *key, const char *value);
bool parent_policy_test_store_erase(const char *key);
void parent_policy_test_store_set_fail_key(const char *key, bool erase);

#define HOST_REQUEST_CAPACITY 16
#define HOST_BODY_CAPACITY 8192

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

static host_request_t host_requests[HOST_REQUEST_CAPACITY];
static size_t host_request_count;
static char host_session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] =
    "session-1";
static char host_device_id[DEVICE_IDENTIFIER_SIZE] = "device-0001";
static char host_base_url[384] = "https://api.clarkhub.cn";
static esp_err_t host_cloud_auth_result = ESP_OK;
static esp_err_t host_copy_session_result = ESP_OK;
static esp_err_t host_identity_result = ESP_OK;
static esp_err_t host_base_url_result = ESP_OK;
static esp_err_t host_transport_security_result = TRANSPORT_SECURITY_OK;
static esp_err_t host_perform_result = ESP_OK;
static volume_control_error_t host_volume_result = VOLUME_CONTROL_OK;
static uint8_t host_last_volume_percent;
static int host_volume_calls;
static int host_unauthorized_calls;
static int host_revoked_calls;
static int host_transport_security_calls;
static int host_http_status = 200;
static char host_http_body[HOST_BODY_CAPACITY] = "{}";
static bool host_client_init_fails;
static int64_t host_time_microseconds = 1000ll * 1000000ll;
static int host_semaphore_storage;

void parent_policy_host_reset(void) {
    memset(host_requests, 0, sizeof(host_requests));
    host_request_count = 0;
    strcpy(host_session_token, "session-1");
    strcpy(host_device_id, "device-0001");
    strcpy(host_base_url, "https://api.clarkhub.cn");
    host_cloud_auth_result = ESP_OK;
    host_copy_session_result = ESP_OK;
    host_identity_result = ESP_OK;
    host_base_url_result = ESP_OK;
    host_transport_security_result = TRANSPORT_SECURITY_OK;
    host_perform_result = ESP_OK;
    host_volume_result = VOLUME_CONTROL_OK;
    host_last_volume_percent = 0;
    host_volume_calls = 0;
    host_unauthorized_calls = 0;
    host_revoked_calls = 0;
    host_transport_security_calls = 0;
    host_http_status = 200;
    strcpy(host_http_body, "{}");
    host_client_init_fails = false;
    parent_policy_test_store_set_fail_key(nullptr, false);
    parent_policy_test_store_set_fail_key(nullptr, true);
    host_time_microseconds = 1000ll * 1000000ll;
}

void parent_policy_host_reset_store(void) {
    parent_policy_test_store_reset();
}

void parent_policy_host_set_session_token(const char *token) {
    if (token == nullptr) {
        host_session_token[0] = '\0';
        return;
    }
    strncpy(host_session_token, token, sizeof(host_session_token) - 1);
    host_session_token[sizeof(host_session_token) - 1] = '\0';
}

void parent_policy_host_set_device_id(const char *device_id) {
    if (device_id == nullptr) {
        host_device_id[0] = '\0';
        return;
    }
    strncpy(host_device_id, device_id, sizeof(host_device_id) - 1);
    host_device_id[sizeof(host_device_id) - 1] = '\0';
}

void parent_policy_host_set_base_url(const char *base_url) {
    if (base_url == nullptr) {
        host_base_url[0] = '\0';
        return;
    }
    strncpy(host_base_url, base_url, sizeof(host_base_url) - 1);
    host_base_url[sizeof(host_base_url) - 1] = '\0';
}

void parent_policy_host_set_cloud_auth_result(esp_err_t result) {
    host_cloud_auth_result = result;
}

void parent_policy_host_set_copy_session_result(esp_err_t result) {
    host_copy_session_result = result;
}

void parent_policy_host_set_identity_result(esp_err_t result) {
    host_identity_result = result;
}

void parent_policy_host_set_base_url_result(esp_err_t result) {
    host_base_url_result = result;
}

void parent_policy_host_set_volume_result(volume_control_error_t result) {
    host_volume_result = result;
}

void parent_policy_host_set_transport_security_result(esp_err_t result) {
    host_transport_security_result = result;
}

void parent_policy_host_set_http_response(int status_code, const char *body) {
    host_http_status = status_code;
    const char *const source = body != nullptr ? body : "";
    strncpy(host_http_body, source, sizeof(host_http_body) - 1);
    host_http_body[sizeof(host_http_body) - 1] = '\0';
}

void parent_policy_host_set_perform_result(esp_err_t result) {
    host_perform_result = result;
}

void parent_policy_host_set_client_init_fails(bool fails) {
    host_client_init_fails = fails;
}

void parent_policy_host_set_store_ready(bool ready) {
    parent_policy_test_store_set_ready(ready);
}

void parent_policy_host_set_store_fail_on_key(const char *key) {
    parent_policy_test_store_set_fail_key(key, false);
}

void parent_policy_host_set_store_fail_on_erase(const char *key) {
    parent_policy_test_store_set_fail_key(key, true);
}

void parent_policy_host_set_time_seconds(int64_t seconds) {
    host_time_microseconds = seconds * 1000000ll;
}

int parent_policy_host_unauthorized_calls(void) {
    return host_unauthorized_calls;
}

int parent_policy_host_revoked_calls(void) {
    return host_revoked_calls;
}

int parent_policy_host_request_count(void) {
    return (int)host_request_count;
}

int parent_policy_host_volume_calls(void) {
    return host_volume_calls;
}

uint8_t parent_policy_host_last_volume_percent(void) {
    return host_last_volume_percent;
}

int parent_policy_host_transport_security_calls(void) {
    return host_transport_security_calls;
}

const char *parent_policy_host_request_url(size_t index) {
    return index < host_request_count ? host_requests[index].url : "";
}

const char *parent_policy_host_request_authorization(size_t index) {
    return index < host_request_count
        ? host_requests[index].authorization
        : "";
}

esp_http_client_method_t parent_policy_host_request_method(size_t index) {
    return index < host_request_count
        ? host_requests[index].method
        : HTTP_METHOD_GET;
}

bool parent_policy_host_store_has_key(const char *key) {
    return parent_policy_test_store_has_key(key);
}

const char *parent_policy_host_store_get(const char *key) {
    return parent_policy_test_store_get(key);
}

esp_err_t cloud_auth_ensure_authenticated(void) {
    return host_cloud_auth_result;
}

esp_err_t cloud_auth_mark_unauthorized(void) {
    host_unauthorized_calls += 1;
    return ESP_OK;
}

esp_err_t cloud_auth_mark_revoked(void) {
    host_revoked_calls += 1;
    return ESP_OK;
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

bool config_store_is_ready(void);

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

int64_t esp_timer_get_time(void) {
    return host_time_microseconds;
}

bool volume_control_is_ready(void) {
    return true;
}

volume_control_error_t volume_control_set_max_percent(
    uint8_t max_volume_percent
) {
    host_volume_calls += 1;
    host_last_volume_percent = max_volume_percent;
    return host_volume_result;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return &host_semaphore_storage;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
    (void)semaphore;
    (void)timeout;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    (void)semaphore;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    (void)semaphore;
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
    host_request_t *const request = &host_requests[host_request_count];
    memset(request, 0, sizeof(*request));
    if (config->url != nullptr) {
        strncpy(request->url, config->url, sizeof(request->url) - 1);
    }
    request->method = config->method;
    client->request = request;
    client->status_code = host_http_status;
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
        esp_http_client_event_t event = {};
        event.event_id = HTTP_EVENT_ON_DATA;
        event.client = client;
        event.data = host_http_body;
        event.data_len = (int)strlen(host_http_body);
        event.user_data = client->user_data;
        client->event_handler(&event);
    }
    return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    return client != nullptr ? client->status_code : 0;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    free(client);
    return ESP_OK;
}

static bool parse_fixed_int(
    const char **cursor,
    int length,
    int minimum,
    int maximum,
    int *value_out
) {
    int value = 0;
    for (int index = 0; index < length; ++index) {
        const char character = (*cursor)[index];
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10 + (character - '0');
    }
    if (value < minimum || value > maximum) {
        return false;
    }
    *cursor += length;
    *value_out = value;
    return true;
}

char *strptime(const char *input, const char *format, struct tm *tm) {
    if (input == nullptr || format == nullptr || tm == nullptr) {
        return nullptr;
    }
    const char *cursor = input;
    const char *pattern = format;
    while (*pattern != '\0') {
        if (*pattern != '%') {
            if (*cursor != *pattern) {
                return nullptr;
            }
            ++cursor;
            ++pattern;
            continue;
        }
        ++pattern;
        int value = 0;
        switch (*pattern) {
            case 'Y':
                if (!parse_fixed_int(&cursor, 4, 1970, 9999, &value)) {
                    return nullptr;
                }
                tm->tm_year = value - 1900;
                break;
            case 'm':
                if (!parse_fixed_int(&cursor, 2, 1, 12, &value)) {
                    return nullptr;
                }
                tm->tm_mon = value - 1;
                break;
            case 'd':
                if (!parse_fixed_int(&cursor, 2, 1, 31, &value)) {
                    return nullptr;
                }
                tm->tm_mday = value;
                break;
            case 'H':
                if (!parse_fixed_int(&cursor, 2, 0, 23, &value)) {
                    return nullptr;
                }
                tm->tm_hour = value;
                break;
            case 'M':
                if (!parse_fixed_int(&cursor, 2, 0, 59, &value)) {
                    return nullptr;
                }
                tm->tm_min = value;
                break;
            case 'S':
                if (!parse_fixed_int(&cursor, 2, 0, 60, &value)) {
                    return nullptr;
                }
                tm->tm_sec = value;
                break;
            case 'Z':
                if (*cursor != 'Z') {
                    return nullptr;
                }
                ++cursor;
                break;
            default:
                return nullptr;
        }
        ++pattern;
    }
    return (char *)cursor;
}

time_t timegm(struct tm *tm) {
    if (tm == nullptr) {
        return (time_t)-1;
    }
    static const int days_before_month[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
    };
    if (tm->tm_mon < 0 || tm->tm_mon > 11 || tm->tm_mday < 1 ||
        tm->tm_mday > 31) {
        return (time_t)-1;
    }
    const int adjusted_year = tm->tm_year + 1900;
    int leap_days = (adjusted_year - 1969) / 4 -
                    (adjusted_year - 1901) / 100 +
                    (adjusted_year - 1601) / 400;
    if (tm->tm_mon <= 1 &&
        ((adjusted_year % 4 == 0 && adjusted_year % 100 != 0) ||
         adjusted_year % 400 == 0)) {
        leap_days -= 1;
    }
    const int64_t days =
        (int64_t)(adjusted_year - 1970) * 365 + leap_days +
        days_before_month[tm->tm_mon] + tm->tm_mday - 1;
    return (time_t)(days * 86400 + tm->tm_hour * 3600 +
                    tm->tm_min * 60 + tm->tm_sec);
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
        case ESP_ERR_INVALID_RESPONSE:
            return "ESP_ERR_INVALID_RESPONSE";
        case ESP_ERR_INVALID_VERSION:
            return "ESP_ERR_INVALID_VERSION";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}

}  // extern "C"
