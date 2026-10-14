#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_event_base_t;

#define ESP_EVENT_ANY_ID (-1)

typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;

typedef enum esp_mqtt_event_id_t {
    MQTT_EVENT_ANY = -1,
    MQTT_EVENT_ERROR = 0,
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_DATA,
} esp_mqtt_event_id_t;

typedef enum esp_mqtt_error_type_t {
    MQTT_ERROR_TYPE_NONE = 0,
    MQTT_ERROR_TYPE_TCP_TRANSPORT,
    MQTT_ERROR_TYPE_CONNECTION_REFUSED,
} esp_mqtt_error_type_t;

typedef struct esp_mqtt_error_codes_t {
    esp_mqtt_error_type_t error_type;
    int connect_return_code;
    int esp_tls_stack_err;
} esp_mqtt_error_codes_t;

typedef struct esp_mqtt_event_t {
    esp_mqtt_event_id_t event_id;
    esp_mqtt_client_handle_t client;
    char *topic;
    int topic_len;
    char *data;
    int data_len;
    int total_data_len;
    esp_mqtt_error_codes_t *error_handle;
} esp_mqtt_event_t;

typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;

typedef struct esp_mqtt_client_config_t {
    struct broker_t {
        struct address_t {
            const char *uri;
        } address;
        struct verification_t {
            const char *certificate;
            bool skip_cert_common_name_check;
        } verification;
    } broker;
    struct credentials_t {
        struct authentication_t {
            const char *certificate;
            const char *key;
        } authentication;
    } credentials;
    struct session_t {
        int keepalive;
        bool disable_clean_session;
    } session;
    struct network_t {
        int reconnect_timeout_ms;
    } network;
} esp_mqtt_client_config_t;

typedef void (*esp_event_handler_t)(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
);

esp_mqtt_client_handle_t esp_mqtt_client_init(
    const esp_mqtt_client_config_t *config
);
esp_err_t esp_mqtt_client_register_event(
    esp_mqtt_client_handle_t client,
    esp_mqtt_event_id_t event,
    esp_event_handler_t event_handler,
    void *event_handler_arg
);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);
int esp_mqtt_client_subscribe(
    esp_mqtt_client_handle_t client,
    const char *topic,
    int qos
);
int esp_mqtt_client_publish(
    esp_mqtt_client_handle_t client,
    const char *topic,
    const char *data,
    int len,
    int qos,
    int retain
);

#ifdef __cplusplus
}
#endif
