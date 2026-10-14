#include "device_runtime_mqtt_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_store.h"
#include "device_identity.h"
#include "device_mqtt.h"
#include "device_mqtt_transport.h"
#include "device_runtime_reporter.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "network_manager.h"

static const char *const TAG = "device_runtime_mqtt";

// Keys are bounded by CONFIG_STORE_KEY_MAX_SIZE (15).
#define DEVICE_RUNTIME_MQTT_URL_KEY "mqtt_url"
#define DEVICE_RUNTIME_MQTT_CA_KEY "mqtt_ca"
#define DEVICE_RUNTIME_MQTT_CERT_KEY "mqtt_cert"
#define DEVICE_RUNTIME_MQTT_PRIVATE_KEY_KEY "mqtt_key"

// The PEM blobs are provisioned artefacts, not secrets embedded in source.
// The bound keeps a corrupt NVS record from forcing an unbounded allocation.
#define DEVICE_RUNTIME_MQTT_URL_SIZE 256
#define DEVICE_RUNTIME_MQTT_PEM_MAX_SIZE 8192
#define DEVICE_RUNTIME_MQTT_COMMAND_QUEUE_LENGTH 4
#define DEVICE_RUNTIME_MQTT_COMMAND_TASK_STACK_SIZE 8192
#define DEVICE_RUNTIME_MQTT_COMMAND_TASK_PRIORITY 4
#define DEVICE_RUNTIME_MQTT_COMMAND_PAYLOAD_MAX \
    (DEVICE_MQTT_COMMAND_DATA_SIZE + 320)

typedef struct {
    char device_id[DEVICE_MQTT_DEVICE_ID_SIZE];
    char payload[DEVICE_RUNTIME_MQTT_COMMAND_PAYLOAD_MAX];
    size_t payload_length;
} device_runtime_mqtt_command_item_t;

static bool device_runtime_mqtt_service_ready;
static bool device_runtime_mqtt_service_observer_registered;
static bool device_runtime_mqtt_service_transport_started;
static char device_runtime_mqtt_service_device_id[DEVICE_MQTT_DEVICE_ID_SIZE];
static QueueHandle_t device_runtime_mqtt_service_command_queue;
static TaskHandle_t device_runtime_mqtt_service_command_task;
static bool device_runtime_mqtt_service_command_task_started;
static volatile bool device_runtime_mqtt_service_command_task_stop;

static esp_err_t device_runtime_mqtt_read_pem(
    const char *key,
    char **value_out
) {
    *value_out = NULL;
    char *buffer = (char *)malloc(DEVICE_RUNTIME_MQTT_PEM_MAX_SIZE + 1);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t value_size = DEVICE_RUNTIME_MQTT_PEM_MAX_SIZE + 1;
    esp_err_t result = config_store_get_blob(key, buffer, &value_size);
    if (result == ESP_ERR_INVALID_SIZE) {
        ESP_LOGW(TAG, "provisioned PEM for %s is too large", key);
        free(buffer);
        return ESP_ERR_INVALID_SIZE;
    }
    if (result != ESP_OK || value_size == 0) {
        memset(buffer, 0, DEVICE_RUNTIME_MQTT_PEM_MAX_SIZE + 1);
        free(buffer);
        return result == ESP_OK ? ESP_ERR_INVALID_STATE : result;
    }
    buffer[value_size] = '\0';
    *value_out = buffer;
    return ESP_OK;
}

static void device_runtime_mqtt_clear_pem(char **value) {
    if (value == NULL || *value == NULL) {
        return;
    }
    memset(*value, 0, strlen(*value));
    free(*value);
    *value = NULL;
}

static esp_err_t device_runtime_mqtt_execute_command(
    const char *device_id,
    const char *payload,
    size_t payload_length
) {
    device_mqtt_command_t command = {};
    if (device_mqtt_parse_command(
            device_id,
            payload,
            payload_length,
            &command
        ) != DEVICE_MQTT_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    const device_runtime_mqtt_command_result_t result =
        device_runtime_reporter_handle_mqtt_command(
            command.device_id,
            command.command_id,
            command.command_type,
            command.request_id,
            command.data,
            command.data_length
        );
    return result == DEVICE_RUNTIME_MQTT_COMMAND_INVALID
               ? ESP_ERR_INVALID_ARG
               : ESP_OK;
}

static void device_runtime_mqtt_command_task(void *argument) {
    (void)argument;
    device_runtime_mqtt_command_item_t item = {};
    while (!device_runtime_mqtt_service_command_task_stop) {
        if (xQueueReceive(
                device_runtime_mqtt_service_command_queue,
                &item,
                pdMS_TO_TICKS(100)
            ) != pdTRUE) {
            continue;
        }
        const esp_err_t result = device_runtime_mqtt_execute_command(
            item.device_id,
            item.payload,
            item.payload_length
        );
        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "queued MQTT command failed: %s",
                esp_err_to_name(result)
            );
        }
        memset(&item, 0, sizeof(item));
    }
    device_runtime_mqtt_service_command_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t device_runtime_mqtt_enqueue_command(
    const char *device_id,
    const char *payload,
    size_t payload_length
) {
    if (device_id == NULL || device_id[0] == '\0' ||
        strlen(device_id) >= sizeof(device_runtime_mqtt_service_device_id) ||
        payload == NULL ||
        payload_length >= sizeof(
            ((device_runtime_mqtt_command_item_t *)0)->payload
        )) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!device_runtime_mqtt_service_command_task_started ||
        device_runtime_mqtt_service_command_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    device_runtime_mqtt_command_item_t item = {};
    snprintf(item.device_id, sizeof(item.device_id), "%s", device_id);
    memcpy(item.payload, payload, payload_length);
    item.payload[payload_length] = '\0';
    item.payload_length = payload_length;
    return xQueueSend(
               device_runtime_mqtt_service_command_queue,
               &item,
               0
           ) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

static esp_err_t device_runtime_mqtt_start_command_task(void) {
    if (device_runtime_mqtt_service_command_task_started) {
        return ESP_OK;
    }
    device_runtime_mqtt_service_command_queue = xQueueCreate(
        DEVICE_RUNTIME_MQTT_COMMAND_QUEUE_LENGTH,
        sizeof(device_runtime_mqtt_command_item_t)
    );
    if (device_runtime_mqtt_service_command_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(
            device_runtime_mqtt_command_task,
            "sprout_mqtt_cmd",
            DEVICE_RUNTIME_MQTT_COMMAND_TASK_STACK_SIZE,
            NULL,
            DEVICE_RUNTIME_MQTT_COMMAND_TASK_PRIORITY,
            &device_runtime_mqtt_service_command_task
        ) != pdPASS) {
        vQueueDelete(device_runtime_mqtt_service_command_queue);
        device_runtime_mqtt_service_command_queue = NULL;
        device_runtime_mqtt_service_command_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    device_runtime_mqtt_service_command_task_started = true;
    device_runtime_mqtt_service_command_task_stop = false;
    return ESP_OK;
}

static void device_runtime_mqtt_stop_command_task(void) {
    if (!device_runtime_mqtt_service_command_task_started) {
        return;
    }
    device_runtime_mqtt_service_command_task_stop = true;
    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(1000);
    while (device_runtime_mqtt_service_command_task != NULL &&
           (int32_t)(xTaskGetTickCount() - deadline) < 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (device_runtime_mqtt_service_command_task != NULL) {
        vTaskDelete(device_runtime_mqtt_service_command_task);
        device_runtime_mqtt_service_command_task = NULL;
    }
    vQueueDelete(device_runtime_mqtt_service_command_queue);
    device_runtime_mqtt_service_command_queue = NULL;
    device_runtime_mqtt_service_command_task_started = false;
}

static esp_err_t device_runtime_mqtt_start_if_configured(void) {
    char url[DEVICE_RUNTIME_MQTT_URL_SIZE] = {0};
    if (config_store_get_string(
            DEVICE_RUNTIME_MQTT_URL_KEY,
            url,
            sizeof(url)
        ) != ESP_OK) {
        ESP_LOGI(TAG, "MQTT command channel is not provisioned");
        return ESP_ERR_NOT_FOUND;
    }

    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    if (device_identity_copy(device_id, sizeof(device_id)) != ESP_OK) {
        ESP_LOGE(TAG, "cannot resolve device identity");
        return ESP_FAIL;
    }

    char *ca_pem = NULL;
    char *cert_pem = NULL;
    char *key_pem = NULL;
    esp_err_t result = device_runtime_mqtt_read_pem(
        DEVICE_RUNTIME_MQTT_CA_KEY,
        &ca_pem
    );
    if (result == ESP_OK) {
        result = device_runtime_mqtt_read_pem(
            DEVICE_RUNTIME_MQTT_CERT_KEY,
            &cert_pem
        );
    }
    if (result == ESP_OK) {
        result = device_runtime_mqtt_read_pem(
            DEVICE_RUNTIME_MQTT_PRIVATE_KEY_KEY,
            &key_pem
        );
    }
    if (result == ESP_OK) {
        snprintf(
            device_runtime_mqtt_service_device_id,
            sizeof(device_runtime_mqtt_service_device_id),
            "%s",
            device_id
        );
        result = device_runtime_reporter_set_mqtt_ack_publisher(
            device_mqtt_transport_publish_ack
        );
    }
    if (result == ESP_OK) {
        result = device_mqtt_transport_start(
            device_runtime_mqtt_service_device_id,
            url,
            ca_pem,
            cert_pem,
            key_pem,
            device_runtime_mqtt_enqueue_command
        );
    }
    if (result != ESP_OK) {
        // Keep the Wi-Fi observer eligible to retry the configured channel
        // after a transient broker or credential failure.
        device_runtime_mqtt_service_device_id[0] = '\0';
    }
    device_runtime_mqtt_clear_pem(&key_pem);
    device_runtime_mqtt_clear_pem(&cert_pem);
    device_runtime_mqtt_clear_pem(&ca_pem);
    memset(url, 0, sizeof(url));

    if (result == ESP_OK) {
        device_runtime_mqtt_service_transport_started = true;
        ESP_LOGI(TAG, "MQTT command channel started");
    } else if (result != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "MQTT command channel failed: %s", esp_err_to_name(result));
    }
    return result;
}

static void device_runtime_mqtt_network_observer(
    network_manager_state_t state,
    void *context
) {
    (void)context;
    if (state != NETWORK_MANAGER_STATE_CONNECTED) {
        return;
    }
    if (device_runtime_mqtt_service_transport_started ||
        device_runtime_mqtt_service_device_id[0] != '\0') {
        // Credentials already resolved; the ESP-MQTT client reconnects on its
        // own, so there is nothing to do on a Wi-Fi flap.
        return;
    }
    (void)device_runtime_mqtt_start_if_configured();
}

esp_err_t device_runtime_mqtt_service_start(void) {
    if (device_runtime_mqtt_service_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t command_task_result =
        device_runtime_mqtt_start_command_task();
    if (command_task_result != ESP_OK) {
        return command_task_result;
    }
    if (!device_runtime_mqtt_service_observer_registered) {
        const esp_err_t observer_result = network_manager_add_state_callback(
            device_runtime_mqtt_network_observer,
            NULL
        );
        if (observer_result != ESP_OK) {
            device_runtime_mqtt_stop_command_task();
            return observer_result;
        }
        device_runtime_mqtt_service_observer_registered = true;
    }
    device_runtime_mqtt_service_ready = true;

    if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED) {
        const esp_err_t result = device_runtime_mqtt_start_if_configured();
        if (result != ESP_OK && result != ESP_ERR_NOT_FOUND) {
            // A configured-but-broken channel is still retried on the next
            // Wi-Fi transition; startup itself remains non-fatal because the
            // authenticated HTTP poll is the recovery path.
            ESP_LOGW(TAG, "initial MQTT start deferred: %s", esp_err_to_name(result));
        }
    }
    return ESP_OK;
}

esp_err_t device_runtime_mqtt_service_stop(void) {
    if (device_runtime_mqtt_service_observer_registered) {
        (void)network_manager_remove_state_callback(
            device_runtime_mqtt_network_observer,
            NULL
        );
        device_runtime_mqtt_service_observer_registered = false;
    }
    if (device_runtime_mqtt_service_transport_started) {
        (void)device_mqtt_transport_stop();
        device_runtime_mqtt_service_transport_started = false;
    }
    device_runtime_mqtt_stop_command_task();
    (void)device_runtime_reporter_set_mqtt_ack_publisher(NULL);
    device_runtime_mqtt_service_device_id[0] = '\0';
    device_runtime_mqtt_service_ready = false;
    return ESP_OK;
}

bool device_runtime_mqtt_service_is_connected(void) {
    return device_runtime_mqtt_service_transport_started &&
           device_mqtt_transport_is_connected();
}
