#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback invoked for every accepted command delivery.
 *
 * The device id is decoded from the authenticated topic prefix, never from the
 * JSON body. The callback must parse and execute the payload through the
 * shared runtime path and must return promptly; the ESP-MQTT event task cannot
 * block for long without delaying the socket.
 */
typedef esp_err_t (*device_mqtt_command_callback_t)(
    const char *device_id,
    const char *payload,
    size_t payload_length
);

/**
 * @brief Open the mutual-TLS MQTT connection and subscribe to commands.
 *
 * The broker URL, broker CA, and the device client certificate and key are
 * supplied by the caller. Nothing is hardcoded: production credentials come
 * from the device provisioning flow and the compiled configuration. The URL
 * must be an `mqtts://` endpoint on the trusted host allowlist and peer plus
 * hostname verification stay enabled. All supplied strings are borrowed for
 * the duration of this call; the transport copies them and retains the copies
 * until the transport is stopped.
 *
 * Passing a callback replaces the previously registered command callback.
 * Starting twice with the same configuration is idempotent; starting again
 * after the transport stopped reconnects with the new credentials.
 */
esp_err_t device_mqtt_transport_start(
    const char *device_id,
    const char *broker_url,
    const char *ca_pem,
    const char *client_certificate_pem,
    const char *client_key_pem,
    device_mqtt_command_callback_t callback
);

/** @brief Close the MQTT connection and clear the command callback. */
esp_err_t device_mqtt_transport_stop(void);

/** @brief Return true while the MQTT client is connected to the broker. */
bool device_mqtt_transport_is_connected(void);

/**
 * @brief Publish one command acknowledgement at QoS 1.
 *
 * The topic is built from the supplied device id. A publish attempted while
 * disconnected returns ESP_ERR_INVALID_STATE so the runtime can fall back to
 * the authenticated HTTP acknowledgement route. Success means the broker
 * acknowledged the QoS 1 publish; enqueueing alone is not treated as delivery.
 */
esp_err_t device_mqtt_transport_publish_ack(
    const char *device_id,
    const char *ack_json
);

#ifdef __cplusplus
}
#endif
