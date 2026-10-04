#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of pending boot events retained across reboots. */
#define DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY 8
/** Maximum number of pending recovery events retained across reboots. */
#define DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY 8

/** Bounded text sizes used in the diagnostic heartbeat extension. */
#define DIAGNOSTIC_REPORTER_EVENT_ID_SIZE 40
#define DIAGNOSTIC_REPORTER_RESET_REASON_SIZE 24
#define DIAGNOSTIC_REPORTER_FIRMWARE_VERSION_SIZE 20
#define DIAGNOSTIC_REPORTER_MODULE_NAME_SIZE 32
#define DIAGNOSTIC_REPORTER_ERROR_CODE_SIZE 32

/** One boot event that remains pending until the platform accepts it. */
typedef struct {
    uint32_t sequence;
    uint32_t uptime_ms;
    uint32_t boot_count;
    char event_id[DIAGNOSTIC_REPORTER_EVENT_ID_SIZE];
    char reset_reason[DIAGNOSTIC_REPORTER_RESET_REASON_SIZE];
    char firmware_version[DIAGNOSTIC_REPORTER_FIRMWARE_VERSION_SIZE];
} diagnostic_reporter_boot_event_t;

/** Latest removable-module failure observed by the runtime reporter. */
typedef struct {
    uint32_t sequence;
    uint32_t failure_count;
    char module_name[DIAGNOSTIC_REPORTER_MODULE_NAME_SIZE];
    char error_code[DIAGNOSTIC_REPORTER_ERROR_CODE_SIZE];
    char firmware_version[DIAGNOSTIC_REPORTER_FIRMWARE_VERSION_SIZE];
} diagnostic_reporter_failure_t;

/** One module recovery event that remains pending until acknowledged. */
typedef struct {
    uint32_t sequence;
    char event_id[DIAGNOSTIC_REPORTER_EVENT_ID_SIZE];
    char module_name[DIAGNOSTIC_REPORTER_MODULE_NAME_SIZE];
    char firmware_version[DIAGNOSTIC_REPORTER_FIRMWARE_VERSION_SIZE];
} diagnostic_reporter_recovery_event_t;

/** One bounded diagnostic snapshot suitable for inclusion in a heartbeat. */
typedef struct {
    size_t boot_event_count;
    diagnostic_reporter_boot_event_t
        boot_events[DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY];
    size_t recovery_event_count;
    diagnostic_reporter_recovery_event_t
        recovery_events[DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY];
    uint32_t newest_sequence;
    uint32_t dropped_boot_events;
    bool has_failure;
    diagnostic_reporter_failure_t failure;
} diagnostic_reporter_snapshot_t;

/**
 * @brief Initialize persistent diagnostic state and record this boot.
 *
 * Requires config_store, error_recovery, and version_info to be initialized.
 * A corrupt or upgraded state record is replaced with a clean bounded record.
 */
esp_err_t diagnostic_reporter_init(void);

/**
 * @brief Copy the current pending diagnostics and observe the latest failure.
 *
 * The returned snapshot is bounded and never contains credentials or child
 * data. The caller must not modify the contents before sending them.
 */
esp_err_t diagnostic_reporter_get_snapshot(
    diagnostic_reporter_snapshot_t *snapshot
);

/**
 * @brief Acknowledge diagnostics included in a successful heartbeat.
 *
 * Records with a sequence greater than through_sequence remain pending. The
 * caller must invoke this only after the platform accepted the heartbeat.
 */
esp_err_t diagnostic_reporter_acknowledge(uint32_t through_sequence);

/** @brief Release runtime resources owned by the module. */
void diagnostic_reporter_shutdown(void);

/** @brief Return the removable-module descriptor for diagnostic_reporter. */
const module_descriptor_t *diagnostic_reporter_module_descriptor(void);

#ifdef __cplusplus
}
#endif
