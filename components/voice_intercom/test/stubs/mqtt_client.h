#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_event_base_t;

typedef struct {
    int32_t msg_id;
    char *data;
    int data_len;
    int total_data_len;
    int current_data_offset;
    char *topic;
} esp_mqtt_event_t;

typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;

enum { MQTT_EVENT_CONNECTED = 0x100 };
enum { MQTT_EVENT_DISCONNECTED = 0x101 };
enum { MQTT_EVENT_DATA = 0x102 };
enum { MQTT_EVENT_ANY = -1 };

typedef void (*esp_event_handler_t)(
    void *handler_arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
);

/**
 * @brief Subset of the ESP-MQTT configuration the module writes.
 *
 * The double mirrors the real nested layout so a field rename in the module
 * fails the host build instead of silently keeping the certificate path
 * unconfigured.
 */
typedef struct {
    struct {
        struct {
            const char *uri;
        } address;
        struct {
            const char *certificate;
        } verification;
    } broker;
    struct {
        const char *client_id;
        struct {
            const char *certificate;
            const char *key;
        } authentication;
    } credentials;
} esp_mqtt_client_config_t;

typedef struct esp_mqtt_client *esp_mqtt_client_handle_t;

esp_mqtt_client_handle_t esp_mqtt_client_init(
    const esp_mqtt_client_config_t *config
);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_register_event(
    esp_mqtt_client_handle_t client,
    int32_t event,
    esp_event_handler_t event_handler,
    void *event_handler_arg
);
int esp_mqtt_client_subscribe_single(
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
