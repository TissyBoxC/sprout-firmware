#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum device id length accepted from a command topic. */
#define DEVICE_MQTT_DEVICE_ID_SIZE 96

/** @brief Maximum command id length accepted from a command event. */
#define DEVICE_MQTT_COMMAND_ID_SIZE 64

/** @brief Maximum command type length accepted from a command event. */
#define DEVICE_MQTT_COMMAND_TYPE_SIZE 32

/** @brief Maximum request id length accepted from a command event. */
#define DEVICE_MQTT_REQUEST_ID_SIZE 128

/**
 * @brief Maximum raw `data` object length retained from a command event.
 *
 * The command envelope is type-agnostic, so the component keeps the verbatim
 * `data` object and lets the executing module parse the fields its command
 * type actually defines. The bound still caps what a hostile broker payload
 * can force the device to buffer.
 */
#define DEVICE_MQTT_COMMAND_DATA_SIZE 1024

/** @brief Number of recent command ids retained for duplicate detection. */
#define DEVICE_MQTT_DEDUPE_CAPACITY 16

/** @brief Stable command channel schema accepted by this component. */
#define DEVICE_MQTT_SCHEMA_VERSION "1.0.0"

/** @brief Stable command event type accepted by this component. */
#define DEVICE_MQTT_COMMAND_EVENT_TYPE "device.command"

/** @brief Stable command acknowledgement event type emitted by the device. */
#define DEVICE_MQTT_COMMAND_ACK_EVENT_TYPE "device.command_ack"

/** @brief Parsed command envelope from `device.command`. */
typedef struct {
    char device_id[DEVICE_MQTT_DEVICE_ID_SIZE];
    char command_id[DEVICE_MQTT_COMMAND_ID_SIZE];
    char command_type[DEVICE_MQTT_COMMAND_TYPE_SIZE];
    char request_id[DEVICE_MQTT_REQUEST_ID_SIZE];
    /**
     * Verbatim `payload.data` JSON object. Empty when the command carries no
     * data object; the executing module parses the fields its command type
     * defines.
     */
    char data[DEVICE_MQTT_COMMAND_DATA_SIZE];
    size_t data_length;
} device_mqtt_command_t;

/** @brief Stable command disposition for execution and acknowledgement. */
typedef enum {
    DEVICE_MQTT_COMMAND_STATUS_NEW = 0,
    DEVICE_MQTT_COMMAND_STATUS_DUPLICATE,
} device_mqtt_command_status_t;

/** @brief Stable rejection reasons for diagnostics and tests. */
typedef enum {
    DEVICE_MQTT_OK = 0,
    DEVICE_MQTT_ERR_NULL_ARGUMENT,
    DEVICE_MQTT_ERR_INVALID_TOPIC,
    DEVICE_MQTT_ERR_DEVICE_MISMATCH,
    DEVICE_MQTT_ERR_INVALID_JSON,
    DEVICE_MQTT_ERR_UNSUPPORTED_SCHEMA,
    DEVICE_MQTT_ERR_UNSUPPORTED_EVENT,
    DEVICE_MQTT_ERR_INVALID_SUBJECT,
    DEVICE_MQTT_ERR_MISSING_FIELD,
    DEVICE_MQTT_ERR_FIELD_TOO_LONG,
    DEVICE_MQTT_ERR_NOT_INITIALIZED,
    DEVICE_MQTT_ERR_STORAGE,
} device_mqtt_error_t;

/**
 * @brief Extract the device id from a platform-to-device command topic.
 *
 * Only `sprout/v1/devices/{device_id}/commands` is accepted. The device id is
 * never taken from the JSON payload, so a message published on another topic
 * cannot impersonate a different device.
 */
device_mqtt_error_t device_mqtt_device_id_from_topic(
    const char *topic,
    char *device_id_out,
    size_t device_id_out_size
);

/**
 * @brief Parse one command JSON payload against the shared MQTT contract.
 *
 * The caller supplies the device id resolved from the authenticated topic.
 * Both the envelope subject and the payload routing fields must agree with it.
 * A payload that fails any contract check is rejected without partial state.
 */
device_mqtt_error_t device_mqtt_parse_command(
    const char *device_id,
    const char *payload,
    size_t payload_length,
    device_mqtt_command_t *command_out
);

/**
 * @brief Record a parsed command as new or duplicate.
 *
 * The ledger is bounded and in-memory; it is emptied when the device reboots.
 * Duplicate suppression is per command id so a QoS 1 retransmission cannot
 * execute an operator command twice.
 */
device_mqtt_error_t device_mqtt_record_command(
    const device_mqtt_command_t *command,
    device_mqtt_command_status_t *status_out
);

/** @brief Return true when a command id is already in the ledger. */
bool device_mqtt_is_duplicate(const char *command_id);

/** @brief Clear the in-memory deduplication ledger. */
void device_mqtt_reset_state(void);

/**
 * @brief Build the acknowledgement envelope for one executed command.
 *
 * The result is a complete `device.command_ack` JSON document ready to publish
 * at QoS 1 on `sprout/v1/devices/{device_id}/commands/ack`. The caller owns the
 * returned buffer and must zero it after use.
 */
esp_err_t device_mqtt_build_command_ack(
    const char *device_id,
    const char *command_id,
    const char *request_id,
    const char *occurred_at,
    const char *status,
    const char *result_code,
    char *json_out,
    size_t json_out_size
);

/** @brief Return the stable string for one device MQTT error. */
const char *device_mqtt_error_name(device_mqtt_error_t error);

/** @brief Initialize the device MQTT command contract component. */
esp_err_t device_mqtt_init(void);

/** @brief Return true when the component initialized successfully. */
bool device_mqtt_is_ready(void);

/** @brief Return the removable-module descriptor for device_mqtt. */
const module_descriptor_t *device_mqtt_module_descriptor(void);

#ifdef __cplusplus
}
#endif
