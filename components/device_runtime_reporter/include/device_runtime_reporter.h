#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable result reported back to the MQTT command transport. */
typedef enum {
    DEVICE_RUNTIME_MQTT_COMMAND_OK = 0,
    DEVICE_RUNTIME_MQTT_COMMAND_DUPLICATE,
    DEVICE_RUNTIME_MQTT_COMMAND_INVALID,
    DEVICE_RUNTIME_MQTT_COMMAND_EXECUTION_FAILED,
} device_runtime_mqtt_command_result_t;

/**
 * @brief Publish one MQTT command acknowledgement.
 *
 * The callback owns the transport and must publish the supplied envelope at
 * QoS 1 on `sprout/v1/devices/{device_id}/commands/ack`. Returning ESP_OK only
 * means the publish was accepted by the transport, not by the platform.
 */
typedef esp_err_t (*device_runtime_mqtt_ack_publisher_t)(
    const char *device_id,
    const char *ack_json
);

/**
 * @brief Initialize periodic runtime reporting.
 *
 * Requires provisioning, cloud authentication, and runtime state modules.
 * The worker waits for a trusted clock and an authenticated session before
 * sending the first heartbeat.
 */
esp_err_t device_runtime_reporter_init(void);

/**
 * @brief Register the transport callback used for MQTT command acknowledgements.
 *
 * Passing NULL clears the callback. The runtime still executes commands and
 * uses the authenticated HTTP acknowledgement route when no callback is set.
 */
esp_err_t device_runtime_reporter_set_mqtt_ack_publisher(
    device_runtime_mqtt_ack_publisher_t publisher
);

/**
 * @brief Execute one parsed MQTT command through the shared runtime path.
 *
 * The raw command id, type, request id, and verbatim `payload.data` object are
 * supplied by the device_mqtt contract parser. The function validates the
 * command type, applies command-specific payload parsing, suppresses repeated
 * command ids, executes the action once, and acknowledges it through the
 * registered MQTT publisher or the authenticated HTTP route. A replayed
 * command id republishes the recorded acknowledgement without re-execution.
 */
device_runtime_mqtt_command_result_t device_runtime_reporter_handle_mqtt_command(
    const char *device_id,
    const char *command_id,
    const char *command_type,
    const char *request_id,
    const char *data,
    size_t data_length
);

/** @brief Return the removable-module descriptor for device_runtime_reporter. */
const module_descriptor_t *device_runtime_reporter_module_descriptor(void);

#ifdef __cplusplus
}
#endif
