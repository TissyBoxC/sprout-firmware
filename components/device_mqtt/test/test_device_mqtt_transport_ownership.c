#include "device_mqtt_transport.h"

#include <assert.h>
#include <string.h>

#include "mqtt_client.h"

static const char *const DEVICE_ID = "device_demo_001";
static const char *const BROKER_URL = "mqtts://broker.example.test:8883";
static const char *const CA_PEM = "ca-pem";
static const char *const CLIENT_CERTIFICATE_PEM = "client-certificate-pem";
static const char *const CLIENT_KEY_PEM = "client-key-pem";

typedef struct {
    const char *uri;
    const char *ca;
    const char *certificate;
    const char *key;
} mock_client_t;

static mock_client_t active_client;
static bool client_started;

esp_err_t test_command_callback(
    const char *device_id,
    const char *payload,
    size_t payload_length
) {
    (void)device_id;
    (void)payload;
    (void)payload_length;
    return ESP_OK;
}

esp_mqtt_client_handle_t esp_mqtt_client_init(
    const esp_mqtt_client_config_t *config
) {
    // Mirrors ESP-MQTT: certificate, key, and URI pointers are retained, not
    // copied. The transport must keep its own copies alive for the client.
    active_client.uri = config->broker.address.uri;
    active_client.ca = config->broker.verification.certificate;
    active_client.certificate = config->credentials.authentication.certificate;
    active_client.key = config->credentials.authentication.key;
    return (esp_mqtt_client_handle_t)&active_client;
}

esp_err_t esp_mqtt_client_register_event(
    esp_mqtt_client_handle_t client,
    esp_mqtt_event_id_t event,
    esp_event_handler_t event_handler,
    void *event_handler_arg
) {
    (void)client;
    (void)event;
    (void)event_handler;
    (void)event_handler_arg;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client) {
    (void)client;
    client_started = true;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client) {
    (void)client;
    client_started = false;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client) {
    (void)client;
    return ESP_OK;
}

int esp_mqtt_client_subscribe(
    esp_mqtt_client_handle_t client,
    const char *topic,
    int qos
) {
    (void)client;
    (void)topic;
    (void)qos;
    return 1;
}

int esp_mqtt_client_publish(
    esp_mqtt_client_handle_t client,
    const char *topic,
    const char *data,
    int len,
    int qos,
    int retain
) {
    (void)client;
    (void)topic;
    (void)data;
    (void)len;
    (void)qos;
    (void)retain;
    return 1;
}

int main(void) {
    char broker_url[64] = {0};
    char ca_pem[16] = {0};
    char certificate_pem[32] = {0};
    char key_pem[16] = {0};
    strcpy(broker_url, BROKER_URL);
    strcpy(ca_pem, CA_PEM);
    strcpy(certificate_pem, CLIENT_CERTIFICATE_PEM);
    strcpy(key_pem, CLIENT_KEY_PEM);

    assert(
        device_mqtt_transport_start(
            DEVICE_ID,
            broker_url,
            ca_pem,
            certificate_pem,
            key_pem,
            test_command_callback
        ) == ESP_OK
    );
    assert(client_started);

    // Caller-owned buffers from the real service are cleared immediately after
    // start returns. The client must still see the transport's retained copy.
    memset(broker_url, 0, sizeof(broker_url));
    memset(ca_pem, 0, sizeof(ca_pem));
    memset(certificate_pem, 0, sizeof(certificate_pem));
    memset(key_pem, 0, sizeof(key_pem));

    assert(strcmp(active_client.uri, BROKER_URL) == 0);
    assert(strcmp(active_client.ca, CA_PEM) == 0);
    assert(strcmp(active_client.certificate, CLIENT_CERTIFICATE_PEM) == 0);
    assert(strcmp(active_client.key, CLIENT_KEY_PEM) == 0);

    assert(device_mqtt_transport_stop() == ESP_OK);
    return 0;
}
