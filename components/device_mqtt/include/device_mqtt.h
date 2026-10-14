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

/** @brief Number of recent command outcomes retained for duplicate detection. */
#define DEVICE_MQTT_DEDUPE_CAPACITY 16

/** @brief Stable command channel schema accepted by this component. */
#define DEVICE_MQTT_SCHEMA_VERSION "1.0.0"

/** @brief Stable command event type accepted by this component. */
#define DEVICE_MQTT_COMMAND_EVENT_TYPE "device.command"

/** @brief Stable command acknowledgement event type emitted by the device. */
#define DEVICE_MQTT_COMMAND_ACK_EVENT_TYPE "device.command_ack"

/** @brief Maximum title length accepted from a display_message command. */
#define DEVICE_MQTT_MESSAGE_TITLE_SIZE 128

/** @brief Maximum body length accepted from a display_message command. */
#define DEVICE_MQTT_MESSAGE_BODY_SIZE 512

/** @brief Maximum severity length accepted from a display_message command. */
#define DEVICE_MQTT_MESSAGE_SEVERITY_SIZE 16

/** @brief Maximum category length accepted from a display_message command. */
#define DEVICE_MQTT_MESSAGE_CATEGORY_SIZE 32

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

/** @brief Parsed `display_message` payload ready for the presenter. */
typedef struct {
    char notification_id[DEVICE_MQTT_COMMAND_ID_SIZE];
    char title[DEVICE_MQTT_MESSAGE_TITLE_SIZE];
    char body[DEVICE_MQTT_MESSAGE_BODY_SIZE];
    char severity[DEVICE_MQTT_MESSAGE_SEVERITY_SIZE];
    char category[DEVICE_MQTT_MESSAGE_CATEGORY_SIZE];
    uint32_t duration_seconds;
} device_mqtt_display_message_t;

/** @brief Parsed `sync_privacy_consent` payload ready for the privacy guard. */
typedef struct {
    bool audio_upload_consent;
    bool image_upload_consent;
    bool conversation_history_consent;
    bool usage_analytics_consent;
} device_mqtt_privacy_consent_t;

/** @brief Stable command disposition for execution and acknowledgement. */
typedef enum {
    DEVICE_MQTT_COMMAND_STATUS_NEW = 0,
    DEVICE_MQTT_COMMAND_STATUS_DUPLICATE,
} device_mqtt_command_status_t;

/** @brief Maximum acknowledgement status length retained by the ledger. */
#define DEVICE_MQTT_ACK_STATUS_SIZE 32

/** @brief Maximum acknowledgement result-code length retained by the ledger. */
#define DEVICE_MQTT_ACK_RESULT_CODE_SIZE 64

/**
 * @brief Persisted execution outcome for one acknowledged command.
 *
 * The runtime records this after execution so a QoS 1 retransmission can
 * republish the same acknowledgement without executing the command again.
 */
typedef struct {
    char command_id[DEVICE_MQTT_COMMAND_ID_SIZE];
    char request_id[DEVICE_MQTT_REQUEST_ID_SIZE];
    char status[DEVICE_MQTT_ACK_STATUS_SIZE];
    char result_code[DEVICE_MQTT_ACK_RESULT_CODE_SIZE];
    bool completed;
} device_mqtt_command_record_t;

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
    DEVICE_MQTT_ERR_UNSUPPORTED_COMMAND,
    DEVICE_MQTT_ERR_INVALID_PAYLOAD,
    DEVICE_MQTT_ERR_NOT_FOUND,
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

/**
 * @brief Look up a command id in the ledger.
 *
 * Returns DEVICE_MQTT_OK and fills command_out when the command has been seen
 * before. Returns DEVICE_MQTT_ERR_NOT_FOUND when it has not. A record with
 * completed=false means the first delivery is still executing and the caller
 * must not run it again.
 */
device_mqtt_error_t device_mqtt_lookup_command(
    const char *command_id,
    device_mqtt_command_record_t *command_out
);

/** @brief Return true when a command id is already in the ledger. */
bool device_mqtt_is_duplicate(const char *command_id);

/**
 * @brief Persist the acknowledgement outcome for one recorded command.
 *
 * The command id must already be in the ledger. Once completed, duplicate
 * deliveries reuse this exact status and result code instead of re-executing.
 */
device_mqtt_error_t device_mqtt_complete_command(
    const char *command_id,
    const char *request_id,
    const char *status,
    const char *result_code
);

/** @brief Clear the in-memory deduplication ledger. */
void device_mqtt_reset_state(void);

/**
 * @brief Parse a `display_message` command's verbatim data object.
 *
 * The returned presenter fields are bounded and zeroed on failure. The caller
 * must not execute the command unless this returns DEVICE_MQTT_OK.
 */
device_mqtt_error_t device_mqtt_parse_display_message(
    const device_mqtt_command_t *command,
    device_mqtt_display_message_t *message_out
);

/**
 * @brief Parse a `sync_privacy_consent` command's verbatim data object.
 *
 * The platform sends one complete consent snapshot. A payload missing any of
 * the four booleans is rejected so a partial delivery cannot leave a
 * withdrawn class enabled.
 */
device_mqtt_error_t device_mqtt_parse_privacy_consent(
    const device_mqtt_command_t *command,
    device_mqtt_privacy_consent_t *consent_out
);

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
