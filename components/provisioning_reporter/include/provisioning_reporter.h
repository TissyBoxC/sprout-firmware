#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of pending provisioning events retained across reboots. */
#define PROVISIONING_REPORTER_EVENT_CAPACITY 16

/** Bounded text sizes used by the provisioning heartbeat extension. */
#define PROVISIONING_REPORTER_EVENT_ID_SIZE 40
#define PROVISIONING_REPORTER_EVENT_TYPE_SIZE 32
#define PROVISIONING_REPORTER_DETAIL_CODE_SIZE 64
#define PROVISIONING_REPORTER_FIRMWARE_VERSION_SIZE 20

/** Stable provisioning and connectivity event types sent to the platform. */
typedef enum {
    PROVISIONING_EVENT_PROVISIONING_STARTED = 0,
    PROVISIONING_EVENT_WIFI_CONFIGURED,
    PROVISIONING_EVENT_WIFI_FAILED,
    PROVISIONING_EVENT_BINDING_COMPLETED,
    PROVISIONING_EVENT_BINDING_REMOVED,
    PROVISIONING_EVENT_NETWORK_RECONNECTED,
    PROVISIONING_EVENT_NETWORK_LOST,
    PROVISIONING_EVENT_TIME_SYNCED,
    PROVISIONING_EVENT_AUTH_REVOKED,
    PROVISIONING_EVENT_AUTH_RESTORED,
    PROVISIONING_EVENT_BINDING_CONFIRMED,
    PROVISIONING_EVENT_BINDING_PENDING,
} provisioning_event_type_t;

/** One bounded provisioning event retained until a heartbeat succeeds. */
typedef struct {
    uint32_t sequence;
    uint32_t duration_ms;
    char event_id[PROVISIONING_REPORTER_EVENT_ID_SIZE];
    char event_type[PROVISIONING_REPORTER_EVENT_TYPE_SIZE];
    char detail_code[PROVISIONING_REPORTER_DETAIL_CODE_SIZE];
    char firmware_version[PROVISIONING_REPORTER_FIRMWARE_VERSION_SIZE];
} provisioning_reporter_event_t;

/** Bounded provisioning snapshot suitable for inclusion in a heartbeat. */
typedef struct {
    size_t event_count;
    provisioning_reporter_event_t
        events[PROVISIONING_REPORTER_EVENT_CAPACITY];
    uint32_t newest_sequence;
    uint32_t dropped;
} provisioning_reporter_snapshot_t;

/** Persistent provisioning facts used by the heartbeat state object. */
typedef struct {
    bool wifi_configured;
    int64_t last_provisioned_epoch;
    char last_detail_code[PROVISIONING_REPORTER_DETAIL_CODE_SIZE];
} provisioning_reporter_status_t;

/**
 * @brief Initialize persistent provisioning event state.
 *
 * Requires config_store and version_info to be initialized. A corrupt state
 * record is replaced with a clean bounded record.
 */
esp_err_t provisioning_reporter_init(void);

/**
 * @brief Record one bounded provisioning or connectivity event.
 *
 * detail_code must be a stable symbolic identifier and must never contain
 * free text, credentials, SSIDs, tokens, audio, images, or child data.
 * duration_ms is the measured elapsed time and is zero for instantaneous
 * transitions.
 */
void provisioning_reporter_record(
    provisioning_event_type_t type,
    const char *detail_code,
    uint32_t duration_ms
);

/**
 * @brief Copy the pending provisioning events.
 *
 * The returned snapshot is bounded and never contains credentials or child
 * data. The caller must not modify the contents before sending them.
 */
esp_err_t provisioning_reporter_get_snapshot(
    provisioning_reporter_snapshot_t *snapshot
);

/**
 * @brief Acknowledge events included in a successful heartbeat.
 *
 * Records with a sequence greater than through_sequence remain pending. The
 * caller must invoke this only after the platform accepted the heartbeat.
 */
esp_err_t provisioning_reporter_acknowledge(uint32_t through_sequence);

/** @brief Copy the persistent provisioning status used by heartbeat JSON. */
esp_err_t provisioning_reporter_get_status(
    provisioning_reporter_status_t *status
);

/** @brief Release runtime resources owned by the module. */
void provisioning_reporter_shutdown(void);

/** @brief Return the removable-module descriptor for provisioning_reporter. */
const module_descriptor_t *provisioning_reporter_module_descriptor(void);

/** @brief Return the stable contract string for an event type. */
const char *provisioning_reporter_event_type_name(
    provisioning_event_type_t type
);

#ifdef __cplusplus
}
#endif
