#include "device_mqtt_transport.h"

#include <string.h>

#include "device_mqtt.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *const TAG = "device_mqtt_transport";

static esp_mqtt_client_handle_t device_mqtt_client;
static bool device_mqtt_connected;
static device_mqtt_command_callback_t device_mqtt_command_callback;
static char device_mqtt_subscribed_device_id[DEVICE_MQTT_DEVICE_ID_SIZE];

static void device_mqtt_subscribe_if_ready(esp_mqtt_client_handle_t client) {
    if (client == NULL || device_mqtt_subscribed_device_id[0] == '\0') {
        return;
    }
    char topic[DEVICE_MQTT_TOPIC_SIZE] = {0};
    if (device_mqtt_build_command_topic(
            device_mqtt_subscribed_device_id,
            topic,
            sizeof(topic)
        ) != DEVICE_MQTT_OK) {
        ESP_LOGE(TAG, "cannot build command topic for subscription");
        return;
    }
    const int message_id = esp_mqtt_client_subscribe(client, topic, 1);
    if (message_id < 0) {
        ESP_LOGE(TAG, "subscribe to command topic failed: %d", message_id);
    } else {
        ESP_LOGI(TAG, "subscribed to command topic");
    }
}

static void device_mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
) {
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event == NULL) {
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            device_mqtt_connected = true;
            device_mqtt_subscribe_if_ready(event->client);
            break;

        case MQTT_EVENT_DISCONNECTED:
            device_mqtt_connected = false;
            break;

        case MQTT_EVENT_DATA: {
            if (event->topic == NULL || event->data == NULL) {
                break;
            }
            if (event->total_data_len != event->data_len) {
                // The runtime command payload is deliberately bounded far
                // below the MQTT buffer, so fragmented deliveries are treated
                // as malformed rather than partially parsed.
                ESP_LOGW(TAG, "rejecting fragmented command payload");
                break;
            }

            char device_id[DEVICE_MQTT_DEVICE_ID_SIZE] = {0};
            char topic[DEVICE_MQTT_TOPIC_SIZE] = {0};
            const int topic_length = event->topic_len < (int)sizeof(topic) - 1
                                         ? event->topic_len
                                         : (int)sizeof(topic) - 1;
            memcpy(topic, event->topic, (size_t)topic_length);
            topic[topic_length] = '\0';

            if (device_mqtt_device_id_from_topic(
                    topic,
                    device_id,
                    sizeof(device_id)
                ) != DEVICE_MQTT_OK) {
                ESP_LOGW(TAG, "rejecting command on unexpected topic");
                break;
            }
            if (device_mqtt_subscribed_device_id[0] != '\0' &&
                strcmp(device_id, device_mqtt_subscribed_device_id) != 0) {
                ESP_LOGW(TAG, "rejecting command for another device");
                break;
            }
            if (device_mqtt_command_callback == NULL) {
                ESP_LOGW(TAG, "no command callback registered");
                break;
            }

            char payload[DEVICE_MQTT_COMMAND_DATA_SIZE + 320] = {0};
            const int payload_length =
                event->data_len < (int)sizeof(payload) - 1
                    ? event->data_len
                    : (int)sizeof(payload) - 1;
            memcpy(payload, event->data, (size_t)payload_length);
            payload[payload_length] = '\0';
            const esp_err_t result = device_mqtt_command_callback(
                device_id,
                payload,
                (size_t)payload_length
            );
            if (result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "command callback rejected delivery: %s",
                    esp_err_to_name(result)
                );
            }
            break;
        }

        case MQTT_EVENT_ERROR:
            if (event->error_handle != NULL &&
                event->error_handle->error_type ==
                    MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(
                    TAG,
                    "broker refused connection: 0x%02x",
                    event->error_handle->connect_return_code
                );
            } else if (event->error_handle != NULL) {
                ESP_LOGE(
                    TAG,
                    "MQTT transport error: type=%d tls=0x%x",
                    event->error_handle->error_type,
                    event->error_handle->esp_tls_stack_err
                );
            }
            break;

        default:
            break;
    }
}

esp_err_t device_mqtt_transport_start(
    const char *device_id,
    const char *broker_url,
    const char *ca_pem,
    const char *client_certificate_pem,
    const char *client_key_pem,
    device_mqtt_command_callback_t callback
) {
    if (device_id == NULL || device_id[0] == '\0' ||
        strlen(device_id) >= sizeof(device_mqtt_subscribed_device_id) ||
        broker_url == NULL || broker_url[0] == '\0' ||
        ca_pem == NULL || ca_pem[0] == '\0' ||
        client_certificate_pem == NULL ||
        client_certificate_pem[0] == '\0' ||
        client_key_pem == NULL || client_key_pem[0] == '\0' ||
        callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(broker_url, "mqtts://", 8) != 0 &&
        strncmp(broker_url, "tls://", 6) != 0) {
        ESP_LOGE(TAG, "broker URL must use MQTTS");
        return ESP_ERR_INVALID_ARG;
    }

    if (device_mqtt_client != NULL) {
        device_mqtt_transport_stop();
    }

    device_mqtt_command_callback = callback;
    device_mqtt_connected = false;
    snprintf(
        device_mqtt_subscribed_device_id,
        sizeof(device_mqtt_subscribed_device_id),
        "%s",
        device_id
    );

    const esp_mqtt_client_config_t client_config = {
        .broker = {
            .address.uri = broker_url,
            .verification = {
                .certificate = ca_pem,
                .skip_cert_common_name_check = false,
            },
        },
        .credentials = {
            .authentication = {
                .certificate = client_certificate_pem,
                .key = client_key_pem,
            },
        },
        .session = {
            .keepalive = 60,
            .disable_clean_session = false,
        },
        .network = {
            .reconnect_timeout_ms = 5000,
        },
    };

    device_mqtt_client = esp_mqtt_client_init(&client_config);
    if (device_mqtt_client == NULL) {
        device_mqtt_command_callback = NULL;
        return ESP_FAIL;
    }
    const esp_err_t register_result = esp_mqtt_client_register_event(
        device_mqtt_client,
        ESP_EVENT_ANY_ID,
        device_mqtt_event_handler,
        NULL
    );
    if (register_result != ESP_OK) {
        esp_mqtt_client_destroy(device_mqtt_client);
        device_mqtt_client = NULL;
        device_mqtt_command_callback = NULL;
        return register_result;
    }
    const esp_err_t start_result = esp_mqtt_client_start(device_mqtt_client);
    if (start_result != ESP_OK) {
        esp_mqtt_client_destroy(device_mqtt_client);
        device_mqtt_client = NULL;
        device_mqtt_command_callback = NULL;
        return start_result;
    }
    return ESP_OK;
}

esp_err_t device_mqtt_transport_stop(void) {
    if (device_mqtt_client == NULL) {
        device_mqtt_connected = false;
        device_mqtt_command_callback = NULL;
        return ESP_OK;
    }
    const esp_err_t result = esp_mqtt_client_stop(device_mqtt_client);
    esp_mqtt_client_destroy(device_mqtt_client);
    device_mqtt_client = NULL;
    device_mqtt_connected = false;
    device_mqtt_command_callback = NULL;
    device_mqtt_subscribed_device_id[0] = '\0';
    return result;
}

bool device_mqtt_transport_is_connected(void) {
    return device_mqtt_client != NULL && device_mqtt_connected;
}

esp_err_t device_mqtt_transport_publish_ack(
    const char *device_id,
    const char *ack_json
) {
    if (device_id == NULL || device_id[0] == '\0' ||
        ack_json == NULL || ack_json[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!device_mqtt_transport_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    char topic[DEVICE_MQTT_TOPIC_SIZE] = {0};
    if (device_mqtt_build_ack_topic(device_id, topic, sizeof(topic)) !=
        DEVICE_MQTT_OK) {
        return ESP_ERR_INVALID_SIZE;
    }
    const int message_id = esp_mqtt_client_publish(
        device_mqtt_client,
        topic,
        ack_json,
        0,
        1,
        0
    );
    return message_id >= 0 ? ESP_OK : ESP_FAIL;
}
