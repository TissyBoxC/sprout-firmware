#include "device_mqtt_transport.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_mqtt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"

static const char *const TAG = "device_mqtt_transport";
// The broker acknowledgement is expected on the next event-loop turn. The
// bound keeps an acknowledgement from turning into an unbounded blocking call.
#define DEVICE_MQTT_ACK_TIMEOUT_MS 1000
#define DEVICE_MQTT_ACK_QUEUE_LENGTH 4

static esp_mqtt_client_handle_t device_mqtt_client;
static bool device_mqtt_connected;
static device_mqtt_command_callback_t device_mqtt_command_callback;
static char device_mqtt_subscribed_device_id[DEVICE_MQTT_DEVICE_ID_SIZE];
static QueueHandle_t device_mqtt_ack_event_queue;
static char *device_mqtt_broker_url;
static char *device_mqtt_ca_pem;
static char *device_mqtt_client_certificate_pem;
static char *device_mqtt_client_key_pem;
static SemaphoreHandle_t device_mqtt_state_mutex;
static StaticSemaphore_t device_mqtt_state_mutex_buffer;
static SemaphoreHandle_t device_mqtt_publish_mutex;
static StaticSemaphore_t device_mqtt_publish_mutex_buffer;
static atomic_flag device_mqtt_lock_init_in_progress = ATOMIC_FLAG_INIT;
static atomic_bool device_mqtt_locks_ready = ATOMIC_VAR_INIT(false);

static void device_mqtt_ensure_locks(void) {
    if (atomic_load_explicit(&device_mqtt_locks_ready, memory_order_acquire)) {
        return;
    }
    if (!atomic_flag_test_and_set_explicit(
            &device_mqtt_lock_init_in_progress,
            memory_order_acq_rel
        )) {
        device_mqtt_state_mutex = xSemaphoreCreateMutexStatic(
            &device_mqtt_state_mutex_buffer
        );
        device_mqtt_publish_mutex = xSemaphoreCreateMutexStatic(
            &device_mqtt_publish_mutex_buffer
        );
        atomic_store_explicit(
            &device_mqtt_locks_ready,
            true,
            memory_order_release
        );
        return;
    }
    while (!atomic_load_explicit(
        &device_mqtt_locks_ready,
        memory_order_acquire
    )) {
        vTaskDelay(1);
    }
}

static void device_mqtt_state_lock(void) {
    device_mqtt_ensure_locks();
    if (device_mqtt_state_mutex != NULL) {
        (void)xSemaphoreTake(device_mqtt_state_mutex, portMAX_DELAY);
    }
}

static void device_mqtt_state_unlock(void) {
    if (device_mqtt_state_mutex != NULL) {
        (void)xSemaphoreGive(device_mqtt_state_mutex);
    }
}

static void device_mqtt_publish_lock(void) {
    device_mqtt_ensure_locks();
    if (device_mqtt_publish_mutex != NULL) {
        (void)xSemaphoreTake(device_mqtt_publish_mutex, portMAX_DELAY);
    }
}

static void device_mqtt_publish_unlock(void) {
    if (device_mqtt_publish_mutex != NULL) {
        (void)xSemaphoreGive(device_mqtt_publish_mutex);
    }
}

static esp_err_t device_mqtt_copy_string(const char *value, char **copy_out) {
    *copy_out = NULL;
    const size_t value_length = strlen(value);
    char *copy = (char *)malloc(value_length + 1);
    if (copy == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, value, value_length + 1);
    *copy_out = copy;
    return ESP_OK;
}

static void device_mqtt_clear_string(char **value) {
    if (value == NULL || *value == NULL) {
        return;
    }
    memset(*value, 0, strlen(*value));
    free(*value);
    *value = NULL;
}

static void device_mqtt_clear_credentials(void) {
    device_mqtt_clear_string(&device_mqtt_client_key_pem);
    device_mqtt_clear_string(&device_mqtt_client_certificate_pem);
    device_mqtt_clear_string(&device_mqtt_ca_pem);
    device_mqtt_clear_string(&device_mqtt_broker_url);
}

static void device_mqtt_clear_ack_event_queue(void) {
    if (device_mqtt_ack_event_queue == NULL) {
        return;
    }
    vQueueDelete(device_mqtt_ack_event_queue);
    device_mqtt_ack_event_queue = NULL;
}

static void device_mqtt_destroy_client(void) {
    if (device_mqtt_client != NULL) {
        esp_mqtt_client_handle_t client = device_mqtt_client;
        device_mqtt_client = NULL;
        (void)esp_mqtt_client_stop(client);
        (void)esp_mqtt_client_destroy(client);
    }
}

static void device_mqtt_release_state(void) {
    device_mqtt_connected = false;
    device_mqtt_command_callback = NULL;
    device_mqtt_subscribed_device_id[0] = '\0';
    device_mqtt_clear_ack_event_queue();
    device_mqtt_clear_credentials();
}

static esp_err_t device_mqtt_copy_credentials(
    const char *broker_url,
    const char *ca_pem,
    const char *client_certificate_pem,
    const char *client_key_pem
) {
    esp_err_t result = device_mqtt_copy_string(
        broker_url,
        &device_mqtt_broker_url
    );
    if (result == ESP_OK) {
        result = device_mqtt_copy_string(ca_pem, &device_mqtt_ca_pem);
    }
    if (result == ESP_OK) {
        result = device_mqtt_copy_string(
            client_certificate_pem,
            &device_mqtt_client_certificate_pem
        );
    }
    if (result == ESP_OK) {
        result = device_mqtt_copy_string(
            client_key_pem,
            &device_mqtt_client_key_pem
        );
    }
    if (result != ESP_OK) {
        device_mqtt_clear_credentials();
    }
    return result;
}

static void device_mqtt_subscribe_if_ready(
    esp_mqtt_client_handle_t client,
    const char *device_id
) {
    if (client == NULL || device_id == NULL || device_id[0] == '\0') {
        return;
    }
    char topic[DEVICE_MQTT_TOPIC_SIZE] = {0};
    if (device_mqtt_build_command_topic(
            device_id,
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
        case MQTT_EVENT_CONNECTED: {
            device_mqtt_state_lock();
            device_mqtt_connected = true;
            char device_id[sizeof(device_mqtt_subscribed_device_id)] = {0};
            snprintf(
                device_id,
                sizeof(device_id),
                "%s",
                device_mqtt_subscribed_device_id
            );
            device_mqtt_state_unlock();
            device_mqtt_subscribe_if_ready(event->client, device_id);
            break;
        }

        case MQTT_EVENT_DISCONNECTED:
            device_mqtt_state_lock();
            device_mqtt_connected = false;
            device_mqtt_state_unlock();
            break;

        case MQTT_EVENT_PUBLISHED: {
            device_mqtt_state_lock();
            QueueHandle_t queue = device_mqtt_ack_event_queue;
            if (queue == NULL) {
                device_mqtt_state_unlock();
                break;
            }
            const int message_id = event->msg_id;
            (void)xQueueSend(
                queue,
                &message_id,
                0
            );
            device_mqtt_state_unlock();
            break;
        }

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

    device_mqtt_publish_lock();
    device_mqtt_state_lock();
    device_mqtt_destroy_client();
    device_mqtt_release_state();
    device_mqtt_state_unlock();
    device_mqtt_state_lock();

    device_mqtt_command_callback = callback;
    device_mqtt_connected = false;
    snprintf(
        device_mqtt_subscribed_device_id,
        sizeof(device_mqtt_subscribed_device_id),
        "%s",
        device_id
    );
    const esp_err_t copy_result = device_mqtt_copy_credentials(
        broker_url,
        ca_pem,
        client_certificate_pem,
        client_key_pem
    );
    if (copy_result != ESP_OK) {
        device_mqtt_command_callback = NULL;
        device_mqtt_subscribed_device_id[0] = '\0';
        device_mqtt_state_unlock();
        device_mqtt_publish_unlock();
        return copy_result;
    }

    device_mqtt_ack_event_queue = xQueueCreate(
        DEVICE_MQTT_ACK_QUEUE_LENGTH,
        sizeof(int)
    );
    if (device_mqtt_ack_event_queue == NULL) {
        device_mqtt_command_callback = NULL;
        device_mqtt_subscribed_device_id[0] = '\0';
        device_mqtt_clear_credentials();
        device_mqtt_state_unlock();
        device_mqtt_publish_unlock();
        return ESP_ERR_NO_MEM;
    }

    const esp_mqtt_client_config_t client_config = {
        .broker = {
            .address.uri = device_mqtt_broker_url,
            .verification = {
                .certificate = device_mqtt_ca_pem,
                .skip_cert_common_name_check = false,
            },
        },
        .credentials = {
            .authentication = {
                .certificate = device_mqtt_client_certificate_pem,
                .key = device_mqtt_client_key_pem,
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
        device_mqtt_subscribed_device_id[0] = '\0';
        device_mqtt_clear_credentials();
        device_mqtt_state_unlock();
        device_mqtt_publish_unlock();
        return ESP_FAIL;
    }
    device_mqtt_state_unlock();
    const esp_err_t register_result = esp_mqtt_client_register_event(
        device_mqtt_client,
        ESP_EVENT_ANY_ID,
        device_mqtt_event_handler,
        NULL
    );
    if (register_result != ESP_OK) {
        device_mqtt_state_lock();
        device_mqtt_destroy_client();
        device_mqtt_release_state();
        device_mqtt_state_unlock();
        device_mqtt_publish_unlock();
        return register_result;
    }
    const esp_err_t start_result = esp_mqtt_client_start(device_mqtt_client);
    if (start_result != ESP_OK) {
        device_mqtt_state_lock();
        device_mqtt_destroy_client();
        device_mqtt_release_state();
        device_mqtt_state_unlock();
        device_mqtt_publish_unlock();
        return start_result;
    }
    device_mqtt_publish_unlock();
    return ESP_OK;
}

esp_err_t device_mqtt_transport_stop(void) {
    device_mqtt_publish_lock();
    device_mqtt_state_lock();
    if (device_mqtt_client == NULL) {
        device_mqtt_release_state();
        device_mqtt_state_unlock();
        device_mqtt_publish_unlock();
        return ESP_OK;
    }
    esp_mqtt_client_handle_t client = device_mqtt_client;
    device_mqtt_client = NULL;
    device_mqtt_connected = false;
    device_mqtt_state_unlock();

    const esp_err_t result = esp_mqtt_client_stop(client);
    (void)esp_mqtt_client_destroy(client);

    device_mqtt_state_lock();
    device_mqtt_release_state();
    device_mqtt_state_unlock();
    device_mqtt_publish_unlock();
    return result;
}

bool device_mqtt_transport_is_connected(void) {
    device_mqtt_state_lock();
    const bool connected =
        device_mqtt_client != NULL &&
        device_mqtt_command_callback != NULL &&
        device_mqtt_connected;
    device_mqtt_state_unlock();
    return connected;
}

esp_err_t device_mqtt_transport_publish_ack(
    const char *device_id,
    const char *ack_json
) {
    if (device_id == NULL || device_id[0] == '\0' ||
        ack_json == NULL || ack_json[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    device_mqtt_state_lock();
    if (device_mqtt_client == NULL ||
        device_mqtt_command_callback == NULL ||
        !device_mqtt_connected) {
        device_mqtt_state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (device_mqtt_ack_event_queue == NULL) {
        device_mqtt_state_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    char topic[DEVICE_MQTT_TOPIC_SIZE] = {0};
    if (device_mqtt_build_ack_topic(device_id, topic, sizeof(topic)) !=
        DEVICE_MQTT_OK) {
        device_mqtt_state_unlock();
        return ESP_ERR_INVALID_SIZE;
    }
    esp_mqtt_client_handle_t client = device_mqtt_client;
    device_mqtt_state_unlock();

    device_mqtt_publish_lock();
    device_mqtt_state_lock();
    if (device_mqtt_client != client || !device_mqtt_connected ||
        device_mqtt_ack_event_queue == NULL) {
        device_mqtt_state_unlock();
        device_mqtt_publish_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    int stale_message_id = 0;
    while (xQueueReceive(device_mqtt_ack_event_queue, &stale_message_id, 0) ==
           pdTRUE) {
    }
    device_mqtt_state_unlock();
    const int message_id = esp_mqtt_client_publish(
        client,
        topic,
        ack_json,
        0,
        1,
        0
    );
    if (message_id < 0) {
        device_mqtt_publish_unlock();
        return ESP_FAIL;
    }

    // ESP-MQTT returns a message id once the publish is queued. Only the
    // matching MQTT_EVENT_PUBLISHED confirms that the broker accepted it.
    // The event may arrive before publish returns, so queue the id in the
    // event handler rather than relying on a transient shared flag.
    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(DEVICE_MQTT_ACK_TIMEOUT_MS);
    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        if ((int32_t)(now - deadline) >= 0) {
            device_mqtt_publish_unlock();
            return ESP_ERR_TIMEOUT;
        }
        int published_message_id = 0;
        device_mqtt_state_lock();
        QueueHandle_t queue = device_mqtt_ack_event_queue;
        device_mqtt_state_unlock();
        if (queue == NULL) {
            device_mqtt_publish_unlock();
            return ESP_ERR_INVALID_STATE;
        }
        if (xQueueReceive(
                queue,
                &published_message_id,
                deadline - now
            ) != pdTRUE) {
            device_mqtt_publish_unlock();
            return ESP_ERR_TIMEOUT;
        }
        if (published_message_id == message_id) {
            device_mqtt_publish_unlock();
            return ESP_OK;
        }
    }
}
