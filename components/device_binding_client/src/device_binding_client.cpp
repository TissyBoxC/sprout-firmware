// ArduinoJson is a header-only C++ library, so this module is compiled as C++.
// Every exported symbol keeps C linkage so the C composition root can register
// the module descriptor without a C++ shim.
#include "ArduinoJson.h"

#include "device_binding_client.h"

extern "C" {

#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "device_capabilities.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/base64.h"

#define DEVICE_BINDING_KEY_BOUND "device_bound"
#define DEVICE_BINDING_KEY_SESSION_TOKEN "device_session"
#define DEVICE_BINDING_KEY_REGISTERED "device_registered"
#define DEVICE_BINDING_PLATFORM_KEY "platform_base_url"
#define DEVICE_BINDING_HTTP_TIMEOUT_MS 15000
#define DEVICE_BINDING_RESPONSE_SIZE 4096
#define DEVICE_BINDING_URL_SIZE 320
#define DEVICE_BINDING_PUBLIC_KEY_TEXT_SIZE 128
#define DEVICE_BINDING_SIGNATURE_TEXT_SIZE 128
#define DEVICE_BINDING_TRUE_VALUE "1"

static const char *const TAG = "device_binding";

typedef struct {
    char body[DEVICE_BINDING_RESPONSE_SIZE];
    size_t body_length;
} device_binding_response_t;

static bool device_binding_is_ready;
static bool device_binding_is_registered;
static bool device_binding_is_bound;
static char device_binding_session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE];
static device_binding_completed_callback_t device_binding_completed_callback;
static void *device_binding_completed_context;

static esp_err_t device_binding_client_mark_bound(void);

/**
 * @brief Persist the build-time platform URL on first boot.
 *
 * Storing the build default keeps one runtime lookup path for both provisioned
 * and factory devices; an explicit HTTPS URL is required or the client stays
 * unusable instead of silently falling back to cleartext.
 */
static esp_err_t device_binding_client_ensure_platform_url(void) {
    const char *const configured_url = CONFIG_DEVICE_BINDING_PLATFORM_BASE_URL;
    if (configured_url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(configured_url, "https://", 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char stored_url[DEVICE_BINDING_URL_SIZE] = {0};
    const esp_err_t read_result = config_store_get_string(
        DEVICE_BINDING_PLATFORM_KEY,
        stored_url,
        sizeof(stored_url)
    );
    if (read_result == ESP_OK) {
        return ESP_OK;
    }
    if (read_result != CONFIG_STORE_ERR_NOT_FOUND) {
        return read_result;
    }
    return config_store_set_string(
        DEVICE_BINDING_PLATFORM_KEY,
        configured_url
    );
}

/**
 * @brief Buffer one HTTP response body for later parsing.
 *
 * A response larger than the buffer is truncated and then rejected by the JSON
 * parser, which is safer than silently accepting a partial platform reply.
 */
static esp_err_t device_binding_http_event_handler(
    esp_http_client_event_t *event
) {
    device_binding_response_t *response =
        (device_binding_response_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL) {
        return ESP_OK;
    }
    if (event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t remaining =
        sizeof(response->body) - 1 - response->body_length;
    const size_t copy_length = (size_t)event->data_len < remaining
                                   ? (size_t)event->data_len
                                   : remaining;
    if (copy_length == 0) {
        return ESP_OK;
    }
    memcpy(
        response->body + response->body_length,
        event->data,
        copy_length
    );
    response->body_length += copy_length;
    response->body[response->body_length] = '\0';
    return ESP_OK;
}

/**
 * @brief Perform one JSON request against the platform.
 *
 * The certificate bundle is always attached, so a TLS handshake failure stays
 * a failure. A cleartext base URL is rejected before any request is sent.
 */
static esp_err_t device_binding_request(
    const char *method,
    const char *path,
    const char *request_body,
    const char *bearer_token,
    JsonDocument *response_document
) {
    char base_url[DEVICE_BINDING_URL_SIZE] = {0};
    esp_err_t result = config_store_get_string(
        DEVICE_BINDING_PLATFORM_KEY,
        base_url,
        sizeof(base_url)
    );
    if (result != ESP_OK) {
        return result;
    }
    if (strncmp(base_url, "https://", 8) != 0) {
        // Cleartext transport is forbidden by the project security baseline.
        ESP_LOGE(TAG, "platform base URL must use HTTPS");
        return ESP_ERR_INVALID_ARG;
    }

    char url[DEVICE_BINDING_URL_SIZE] = {0};
    const int written = snprintf(url, sizeof(url), "%s%s", base_url, path);
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    device_binding_response_t response{};
    esp_http_client_config_t client_config = {};
    client_config.url = url;
    client_config.method = HTTP_METHOD_GET;
    client_config.timeout_ms = DEVICE_BINDING_HTTP_TIMEOUT_MS;
    client_config.event_handler = device_binding_http_event_handler;
    client_config.user_data = &response;
    // The certificate bundle makes a failed TLS handshake fatal instead of
    // silently falling back to an unverified connection.
    client_config.crt_bundle_attach = esp_crt_bundle_attach;
    if (strcmp(method, "POST") == 0) {
        client_config.method = HTTP_METHOD_POST;
    } else if (strcmp(method, "PUT") == 0) {
        client_config.method = HTTP_METHOD_PUT;
    }

    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (client == NULL) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    if (bearer_token != NULL && bearer_token[0] != '\0') {
        char authorization[DEVICE_BINDING_SESSION_TOKEN_SIZE + 16] = {0};
        const int header_written = snprintf(
            authorization,
            sizeof(authorization),
            "Bearer %s",
            bearer_token
        );
        if (header_written > 0 &&
            (size_t)header_written < sizeof(authorization)) {
            esp_http_client_set_header(client, "Authorization", authorization);
        }
    }
    if (request_body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(
            client,
            request_body,
            (int)strlen(request_body)
        );
    }

    result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (result != ESP_OK) {
        ESP_LOGW(TAG, "platform request failed: %s", esp_err_to_name(result));
        return result;
    }
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "platform returned status %d", status_code);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (response_document == NULL) {
        return ESP_OK;
    }

    JsonDocument envelope;
    if (deserializeJson(envelope, response.body) != DeserializationError::Ok) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    JsonVariant data = envelope["data"];
    if (!data.is<JsonObjectConst>()) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    response_document->set(data);
    return ESP_OK;
}

static esp_err_t device_binding_encode_base64(
    const unsigned char *input,
    size_t input_size,
    char *output,
    size_t output_size
) {
    size_t encoded_length = 0;
    const int result = mbedtls_base64_encode(
        (unsigned char *)output,
        output_size,
        &encoded_length,
        input,
        input_size
    );
    if (result != 0) {
        return result == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL
                   ? ESP_ERR_INVALID_SIZE
                   : ESP_FAIL;
    }
    output[encoded_length] = '\0';
    return ESP_OK;
}

/**
 * @brief Copy one required string field out of a platform response object.
 *
 * A missing or oversized field is a rejected response, never a silent default,
 * because both cases mean the device would proceed with the wrong identity.
 */
static esp_err_t device_binding_copy_json_string(
    JsonVariantConst object,
    const char *field,
    char *output,
    size_t output_size
) {
    const char *const text = object[field].as<const char *>();
    if (text == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const size_t length = strlen(text);
    if (length + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, text, length + 1);
    return ESP_OK;
}

esp_err_t device_binding_client_init(void) {
    if (device_binding_is_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t url_result = device_binding_client_ensure_platform_url();
    if (url_result != ESP_OK) {
        return url_result;
    }
    device_binding_is_ready = true;
    return device_binding_client_refresh_state();
}

esp_err_t device_binding_client_register(
    const char *registration_token,
    const char *hardware_model,
    const char *firmware_version
) {
    if (!device_binding_is_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (registration_token == NULL || registration_token[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    esp_err_t result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        return result;
    }
    unsigned char public_key[DEVICE_IDENTITY_PUBLIC_KEY_BYTES] = {0};
    result = device_identity_copy_public_key(public_key, sizeof(public_key));
    if (result != ESP_OK) {
        return result;
    }
    char public_key_text[DEVICE_BINDING_PUBLIC_KEY_TEXT_SIZE] = {0};
    result = device_binding_encode_base64(
        public_key,
        sizeof(public_key),
        public_key_text,
        sizeof(public_key_text)
    );
    memset(public_key, 0, sizeof(public_key));
    if (result != ESP_OK) {
        return result;
    }

    JsonDocument payload;
    payload["registration_token"] = registration_token;
    payload["device_id"] = device_id;
    payload["hardware_model"] = hardware_model != NULL ? hardware_model : "";
    payload["firmware_version"] =
        firmware_version != NULL ? firmware_version : "";
    payload["public_key"] = public_key_text;
    JsonArray capability_array = payload["capabilities"].to<JsonArray>();
    const device_capability_bitmap_t bitmap = device_capabilities_get();
    // Report exactly the capabilities compiled into this image so a build
    // without a hardware module never advertises that module to the platform.
    for (int capability = 0; capability < DEVICE_CAPABILITY_COUNT; ++capability) {
        if (!device_capabilities_has(bitmap, (device_capability_t)capability)) {
            continue;
        }
        const char *const capability_name =
            device_capability_name((device_capability_t)capability);
        if (capability_name != NULL) {
            capability_array.add(capability_name);
        }
    }

    char request_body[DEVICE_BINDING_RESPONSE_SIZE] = {0};
    const size_t request_length = serializeJson(payload, request_body);
    if (request_length == 0 || request_length >= sizeof(request_body)) {
        return ESP_ERR_INVALID_SIZE;
    }

    result = device_binding_request(
        "POST",
        "/api/v1/devices/register",
        request_body,
        NULL,
        NULL
    );
    memset(request_body, 0, sizeof(request_body));
    if (result != ESP_OK) {
        return result;
    }
    device_binding_is_registered = true;
    return config_store_set_string(
        DEVICE_BINDING_KEY_REGISTERED,
        DEVICE_BINDING_TRUE_VALUE
    );
}

esp_err_t device_binding_client_authenticate(void) {
    if (!device_binding_is_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!device_binding_is_registered) {
        return ESP_ERR_INVALID_STATE;
    }

    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    esp_err_t result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        return result;
    }

    JsonDocument challenge_payload;
    challenge_payload["device_id"] = device_id;
    char challenge_body[DEVICE_BINDING_RESPONSE_SIZE] = {0};
    if (serializeJson(challenge_payload, challenge_body) == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    JsonDocument challenge_response;
    result = device_binding_request(
        "POST",
        "/api/v1/devices/auth/challenge",
        challenge_body,
        NULL,
        &challenge_response
    );
    if (result != ESP_OK) {
        return result;
    }
    char nonce[DEVICE_BINDING_TOKEN_SIZE] = {0};
    result = device_binding_copy_json_string(
        challenge_response.as<JsonVariantConst>(),
        "nonce",
        nonce,
        sizeof(nonce)
    );
    if (result != ESP_OK) {
        return result;
    }

    char message[DEVICE_IDENTIFIER_SIZE + DEVICE_BINDING_TOKEN_SIZE + 2] = {0};
    const int message_length = snprintf(
        message,
        sizeof(message),
        "%s.%s",
        device_id,
        nonce
    );
    if (message_length <= 0 || (size_t)message_length >= sizeof(message)) {
        return ESP_ERR_INVALID_SIZE;
    }
    unsigned char signature[DEVICE_IDENTITY_SIGNATURE_BYTES] = {0};
    result = device_identity_sign(
        (const uint8_t *)message,
        (size_t)message_length,
        signature,
        sizeof(signature)
    );
    if (result != ESP_OK) {
        return result;
    }
    char signature_text[DEVICE_BINDING_SIGNATURE_TEXT_SIZE] = {0};
    result = device_binding_encode_base64(
        signature,
        sizeof(signature),
        signature_text,
        sizeof(signature_text)
    );
    memset(signature, 0, sizeof(signature));
    if (result != ESP_OK) {
        return result;
    }

    JsonDocument complete_payload;
    complete_payload["device_id"] = device_id;
    complete_payload["nonce"] = nonce;
    complete_payload["signature"] = signature_text;
    char complete_body[DEVICE_BINDING_RESPONSE_SIZE] = {0};
    if (serializeJson(complete_payload, complete_body) == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    JsonDocument complete_response;
    result = device_binding_request(
        "POST",
        "/api/v1/devices/auth/complete",
        complete_body,
        NULL,
        &complete_response
    );
    if (result != ESP_OK) {
        return result;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    result = device_binding_copy_json_string(
        complete_response.as<JsonVariantConst>(),
        "device_session_token",
        session_token,
        sizeof(session_token)
    );
    if (result != ESP_OK) {
        return result;
    }

    result = config_store_set_string(
        DEVICE_BINDING_KEY_SESSION_TOKEN,
        session_token
    );
    if (result == ESP_OK) {
        memcpy(
            device_binding_session_token,
            session_token,
            sizeof(device_binding_session_token)
        );
    }
    memset(session_token, 0, sizeof(session_token));
    return result;
}

esp_err_t device_binding_client_create_provisioning_token(
    char *output,
    size_t output_size
) {
    if (!device_binding_is_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device_binding_session_token[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    esp_err_t result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        return result;
    }
    char path[DEVICE_BINDING_URL_SIZE] = {0};
    const int written = snprintf(
        path,
        sizeof(path),
        "/api/v1/devices/%s/provisioning-token",
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    JsonDocument response;
    result = device_binding_request(
        "POST",
        path,
        NULL,
        device_binding_session_token,
        &response
    );
    if (result != ESP_OK) {
        return result;
    }
    return device_binding_copy_json_string(
        response.as<JsonVariantConst>(),
        "token",
        output,
        output_size
    );
}

bool device_binding_client_is_bound(void) {
    return device_binding_is_bound;
}

esp_err_t device_binding_client_check_binding(bool *is_bound_out) {
    if (is_bound_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device_binding_session_token[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    esp_err_t result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        return result;
    }
    char path[DEVICE_BINDING_URL_SIZE] = {0};
    const int written = snprintf(
        path,
        sizeof(path),
        "/api/v1/devices/%s/binding-status",
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    JsonDocument response;
    result = device_binding_request(
        "GET",
        path,
        NULL,
        device_binding_session_token,
        &response
    );
    if (result != ESP_OK) {
        return result;
    }
    if (!response["is_bound"].is<bool>()) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const bool is_bound = response["is_bound"].as<bool>();
    *is_bound_out = is_bound;
    if (is_bound && !device_binding_is_bound) {
        // Persist before notifying so a reboot keeps the completed state.
        return device_binding_client_mark_bound();
    }
    return ESP_OK;
}

esp_err_t device_binding_client_set_completion_callback(
    device_binding_completed_callback_t callback,
    void *context
) {
    device_binding_completed_callback = callback;
    device_binding_completed_context = context;
    return ESP_OK;
}

esp_err_t device_binding_client_refresh_state(void) {
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    char stored_value[8] = {0};
    esp_err_t result = config_store_get_string(
        DEVICE_BINDING_KEY_BOUND,
        stored_value,
        sizeof(stored_value)
    );
    if (result == ESP_OK) {
        device_binding_is_bound =
            strcmp(stored_value, DEVICE_BINDING_TRUE_VALUE) == 0;
    } else if (result == CONFIG_STORE_ERR_NOT_FOUND) {
        device_binding_is_bound = false;
        result = ESP_OK;
    }
    if (result != ESP_OK) {
        return result;
    }

    memset(stored_value, 0, sizeof(stored_value));
    result = config_store_get_string(
        DEVICE_BINDING_KEY_REGISTERED,
        stored_value,
        sizeof(stored_value)
    );
    if (result == ESP_OK) {
        device_binding_is_registered =
            strcmp(stored_value, DEVICE_BINDING_TRUE_VALUE) == 0;
    } else if (result == CONFIG_STORE_ERR_NOT_FOUND) {
        device_binding_is_registered = false;
        result = ESP_OK;
    }
    if (result != ESP_OK) {
        return result;
    }

    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    result = config_store_get_string(
        DEVICE_BINDING_KEY_SESSION_TOKEN,
        session_token,
        sizeof(session_token)
    );
    if (result == ESP_OK) {
        memcpy(
            device_binding_session_token,
            session_token,
            sizeof(device_binding_session_token)
        );
    } else if (result == CONFIG_STORE_ERR_NOT_FOUND) {
        device_binding_session_token[0] = '\0';
        result = ESP_OK;
    }
    memset(session_token, 0, sizeof(session_token));
    return result;
}

static esp_err_t device_binding_client_mark_bound(void) {
    const esp_err_t result = config_store_set_string(
        DEVICE_BINDING_KEY_BOUND,
        DEVICE_BINDING_TRUE_VALUE
    );
    if (result != ESP_OK) {
        return result;
    }
    device_binding_is_bound = true;
    if (device_binding_completed_callback != NULL) {
        device_binding_completed_callback(true, device_binding_completed_context);
    }
    return ESP_OK;
}

const module_descriptor_t *device_binding_client_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "device_binding_client",
        .version = "1.0.0",
        .initialize = device_binding_client_init,
        .shutdown = NULL,
    };
    return &descriptor;
}

}  // extern "C"
