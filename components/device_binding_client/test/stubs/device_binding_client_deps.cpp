#include "device_binding_client_host.h"

#include "config_store.h"
#include "device_capabilities.h"
#include "device_identity.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "mbedtls/base64.h"
#include "transport_security.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {

#define HOST_REQUEST_CAPACITY 32
#define HOST_RESPONSE_COUNT 16
#define HOST_STORE_ENTRY_COUNT 32
#define HOST_STORE_KEY_SIZE 32
#define HOST_STORE_VALUE_SIZE 512

typedef struct {
    char key[HOST_STORE_KEY_SIZE];
    char value[HOST_STORE_VALUE_SIZE];
    bool used;
} host_store_entry_t;

typedef struct {
    char url[512];
    char authorization[192];
    char body[2048];
    esp_http_client_method_t method;
} host_request_t;

typedef struct esp_http_client {
    host_request_t *request;
    int status_code;
    size_t response_index;
    http_event_handle_cb event_handler;
    void *user_data;
} host_client_t;

static host_store_entry_t host_store[HOST_STORE_ENTRY_COUNT];
static host_request_t host_requests[HOST_REQUEST_CAPACITY];
static size_t host_request_count;
static bool host_config_ready = true;
static esp_err_t host_identity_result = ESP_OK;
static esp_err_t host_public_key_result = ESP_OK;
static esp_err_t host_sign_result = ESP_OK;
static int host_base64_result = 0;
static esp_err_t host_transport_security_result = TRANSPORT_SECURITY_OK;
static bool host_client_init_fails;
static esp_err_t host_perform_result = ESP_OK;
static int host_status_codes[HOST_RESPONSE_COUNT] = {200};
static const char *host_bodies[HOST_RESPONSE_COUNT] = {"{}"};
static size_t host_response_count = 1;
static esp_err_t host_store_set_string_result = ESP_OK;
static esp_err_t host_store_erase_result = ESP_OK;
static char host_app_version[32] = "0.1.0-test";
static char host_last_signed_message[1024];
static size_t host_last_signed_message_size;
static int host_base64_encode_calls;
static int host_transport_security_calls;

void device_binding_client_host_seed_string(
    const char *key,
    const char *value
);

static host_store_entry_t *host_find_store(const char *key) {
    if (key == nullptr) {
        return nullptr;
    }
    for (size_t index = 0; index < HOST_STORE_ENTRY_COUNT; ++index) {
        if (host_store[index].used &&
            strcmp(host_store[index].key, key) == 0) {
            return &host_store[index];
        }
    }
    return nullptr;
}

static host_store_entry_t *host_allocate_store(const char *key) {
    host_store_entry_t *existing = host_find_store(key);
    if (existing != nullptr) {
        return existing;
    }
    if (key == nullptr || strlen(key) >= HOST_STORE_KEY_SIZE) {
        return nullptr;
    }
    for (size_t index = 0; index < HOST_STORE_ENTRY_COUNT; ++index) {
        if (!host_store[index].used) {
            memset(&host_store[index], 0, sizeof(host_store[index]));
            host_store[index].used = true;
            strncpy(host_store[index].key, key, sizeof(host_store[index].key) - 1);
            return &host_store[index];
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

void device_binding_client_host_reset(void) {
    memset(host_store, 0, sizeof(host_store));
    memset(host_requests, 0, sizeof(host_requests));
    host_request_count = 0;
    host_config_ready = true;
    host_identity_result = ESP_OK;
    host_public_key_result = ESP_OK;
    host_sign_result = ESP_OK;
    host_base64_result = 0;
    host_transport_security_result = TRANSPORT_SECURITY_OK;
    host_client_init_fails = false;
    host_perform_result = ESP_OK;
    host_status_codes[0] = 200;
    host_bodies[0] = "{}";
    host_response_count = 1;
    host_store_set_string_result = ESP_OK;
    host_store_erase_result = ESP_OK;
    strcpy(host_app_version, "0.1.0-test");
    memset(host_last_signed_message, 0, sizeof(host_last_signed_message));
    host_last_signed_message_size = 0;
    host_base64_encode_calls = 0;
    device_binding_client_host_seed_string(
        "platform_url",
        "https://api.clarkhub.cn"
    );
}

void device_binding_client_host_set_config_ready(bool ready) {
    host_config_ready = ready;
}

void device_binding_client_host_set_platform_url(const char *url) {
    device_binding_client_host_seed_string(
        "platform_url",
        url != nullptr ? url : ""
    );
}

void device_binding_client_host_set_identity_result(esp_err_t result) {
    host_identity_result = result;
}

void device_binding_client_host_set_public_key_result(esp_err_t result) {
    host_public_key_result = result;
}

void device_binding_client_host_set_sign_result(esp_err_t result) {
    host_sign_result = result;
}

void device_binding_client_host_set_base64_result(int result) {
    host_base64_result = result;
}

void device_binding_client_host_set_transport_security_result(esp_err_t result) {
    host_transport_security_result = result;
}

void device_binding_client_host_set_client_init_fails(bool fails) {
    host_client_init_fails = fails;
}

void device_binding_client_host_set_perform_result(esp_err_t result) {
    host_perform_result = result;
}

void device_binding_client_host_set_http_sequence(
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

void device_binding_client_host_set_store_set_string_result(esp_err_t result) {
    host_store_set_string_result = result;
}

void device_binding_client_host_set_store_erase_result(esp_err_t result) {
    host_store_erase_result = result;
}

void device_binding_client_host_set_app_version(const char *version) {
    if (version == nullptr) {
        host_app_version[0] = '\0';
        return;
    }
    strncpy(host_app_version, version, sizeof(host_app_version) - 1);
    host_app_version[sizeof(host_app_version) - 1] = '\0';
}

void device_binding_client_host_seed_string(const char *key, const char *value) {
    if (key == nullptr || value == nullptr) {
        return;
    }
    host_store_entry_t *entry = host_allocate_store(key);
    if (entry == nullptr) {
        return;
    }
    strncpy(entry->value, value, sizeof(entry->value) - 1);
    entry->value[sizeof(entry->value) - 1] = '\0';
}

int device_binding_client_host_transport_security_calls(void) {
    return host_transport_security_calls;
}

size_t device_binding_client_host_request_count(void) {
    return host_request_count;
}

const char *device_binding_client_host_request_url(size_t index) {
    return index < host_request_count ? host_requests[index].url : "";
}

const char *device_binding_client_host_request_body(size_t index) {
    return index < host_request_count ? host_requests[index].body : "";
}

const char *device_binding_client_host_request_authorization(size_t index) {
    return index < host_request_count
        ? host_requests[index].authorization
        : "";
}

esp_http_client_method_t device_binding_client_host_request_method(size_t index) {
    return index < host_request_count
        ? host_requests[index].method
        : HTTP_METHOD_GET;
}

const char *device_binding_client_host_stored_string(const char *key) {
    host_store_entry_t *entry = host_find_store(key);
    return entry != nullptr ? entry->value : "";
}

bool device_binding_client_host_has_key(const char *key) {
    return host_find_store(key) != nullptr;
}

const char *device_binding_client_host_last_signed_message(void) {
    return host_last_signed_message;
}

size_t device_binding_client_host_last_signed_message_size(void) {
    return host_last_signed_message_size;
}

int device_binding_client_host_base64_encode_calls(void) {
    return host_base64_encode_calls;
}

bool config_store_is_ready(void) {
    return host_config_ready;
}

esp_err_t config_store_init(void) {
    return host_config_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    if (key == nullptr || output == nullptr || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    host_store_entry_t *entry = host_find_store(key);
    if (entry == nullptr) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (strlen(entry->value) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    strcpy(output, entry->value);
    return ESP_OK;
}

esp_err_t config_store_set_string(const char *key, const char *value) {
    if (host_store_set_string_result != ESP_OK) {
        return host_store_set_string_result;
    }
    if (key == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    host_store_entry_t *entry = host_allocate_store(key);
    if (entry == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    strncpy(entry->value, value, sizeof(entry->value) - 1);
    entry->value[sizeof(entry->value) - 1] = '\0';
    return ESP_OK;
}

esp_err_t config_store_erase_key(const char *key) {
    if (host_store_erase_result != ESP_OK) {
        return host_store_erase_result;
    }
    host_store_entry_t *entry = host_find_store(key);
    if (entry != nullptr) {
        memset(entry, 0, sizeof(*entry));
    }
    return ESP_OK;
}

esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t value_size
) {
    (void)key;
    (void)value;
    (void)value_size;
    return ESP_OK;
}

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *value_size_in_out
) {
    (void)key;
    (void)output;
    (void)value_size_in_out;
    return CONFIG_STORE_ERR_NOT_FOUND;
}

bool device_capabilities_has(
    device_capability_bitmap_t capabilities,
    device_capability_t capability
) {
    return (capabilities & (1u << (unsigned)capability)) != 0u;
}

device_capability_bitmap_t device_capabilities_get(void) {
    return (1u << DEVICE_CAPABILITY_AUDIO_INPUT) |
           (1u << DEVICE_CAPABILITY_AUDIO_OUTPUT) |
           (1u << DEVICE_CAPABILITY_WIFI) |
           (1u << DEVICE_CAPABILITY_BLUETOOTH_AUDIO);
}

const char *device_capability_name(device_capability_t capability) {
    switch (capability) {
        case DEVICE_CAPABILITY_AUDIO_INPUT:
            return "audio_input";
        case DEVICE_CAPABILITY_AUDIO_OUTPUT:
            return "audio_output";
        case DEVICE_CAPABILITY_WIFI:
            return "wifi";
        case DEVICE_CAPABILITY_BLUETOOTH_AUDIO:
            return "bluetooth_audio";
        default:
            return nullptr;
    }
}

esp_err_t device_identity_copy(char *output, size_t output_size) {
    if (host_identity_result != ESP_OK) {
        return host_identity_result;
    }
    if (output == nullptr || output_size < 16) {
        return ESP_ERR_INVALID_SIZE;
    }
    strcpy(output, "device-0001");
    return ESP_OK;
}

esp_err_t device_identity_copy_public_key(
    uint8_t *output,
    size_t output_size
) {
    if (host_public_key_result != ESP_OK) {
        return host_public_key_result;
    }
    if (output == nullptr || output_size < 65) {
        return ESP_ERR_INVALID_SIZE;
    }
    output[0] = 0x04;
    for (size_t index = 1; index < 65; ++index) {
        output[index] = (uint8_t)index;
    }
    return ESP_OK;
}

esp_err_t device_identity_sign(
    const uint8_t *message,
    size_t message_size,
    uint8_t *signature,
    size_t signature_size
) {
    if (host_sign_result != ESP_OK) {
        return host_sign_result;
    }
    if (message == nullptr || signature == nullptr || signature_size < 64) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(host_last_signed_message, message,
           message_size < sizeof(host_last_signed_message)
               ? message_size
               : sizeof(host_last_signed_message) - 1);
    host_last_signed_message_size = message_size;
    for (size_t index = 0; index < 64; ++index) {
        signature[index] = (uint8_t)(index + 1);
    }
    return ESP_OK;
}

int mbedtls_base64_encode(
    unsigned char *dst,
    size_t dlen,
    size_t *olen,
    const unsigned char *src,
    size_t slen
) {
    ++host_base64_encode_calls;
    if (host_base64_result != 0) {
        return host_base64_result;
    }
    if (dst == nullptr || olen == nullptr || src == nullptr) {
        return -1;
    }
    // The host test only needs a deterministic, bounded Base64-shaped value.
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t encoded_size = ((slen + 2) / 3) * 4;
    if (dlen <= encoded_size) {
        return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
    }
    size_t output_index = 0;
    for (size_t index = 0; index < slen; index += 3) {
        unsigned value = (unsigned)src[index] << 16;
        const unsigned second = index + 1 < slen ? src[index + 1] : 0;
        const unsigned third = index + 2 < slen ? src[index + 2] : 0;
        value |= second << 8;
        value |= third;
        dst[output_index++] = alphabet[(value >> 18) & 0x3F];
        dst[output_index++] = alphabet[(value >> 12) & 0x3F];
        dst[output_index++] = index + 1 < slen
            ? alphabet[(value >> 6) & 0x3F]
            : '=';
        dst[output_index++] = index + 2 < slen
            ? alphabet[value & 0x3F]
            : '=';
    }
    dst[output_index] = '\0';
    *olen = encoded_size;
    return 0;
}

transport_security_error_t transport_security_validate_outbound(
    const char *url,
    transport_security_endpoint_t *endpoint
) {
    (void)url;
    (void)endpoint;
    ++host_transport_security_calls;
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
    return client == nullptr ? 0 : client->status_code;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    free(client);
    return ESP_OK;
}

const esp_app_desc_t *esp_app_get_description(void) {
    static esp_app_desc_t description;
    strncpy(description.version, host_app_version, sizeof(description.version) - 1);
    description.version[sizeof(description.version) - 1] = '\0';
    return &description;
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
        case ESP_ERR_INVALID_RESPONSE:
            return "ESP_ERR_INVALID_RESPONSE";
        default:
            return "ESP_ERR_UNKNOWN";
    }
}

}  // extern "C"
