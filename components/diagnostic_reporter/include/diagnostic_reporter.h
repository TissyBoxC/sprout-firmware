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
/** Maximum number of pending user interaction events retained across reboots. */
#define DIAGNOSTIC_REPORTER_INTERACTION_EVENT_CAPACITY 16
/** Maximum number of pending crash signatures retained across reboots. */
#define DIAGNOSTIC_REPORTER_CRASH_EVENT_CAPACITY 8

/** Bounded text sizes used in the diagnostic heartbeat extension. */
#define DIAGNOSTIC_REPORTER_EVENT_ID_SIZE 40
#define DIAGNOSTIC_REPORTER_RESET_REASON_SIZE 24
#define DIAGNOSTIC_REPORTER_FIRMWARE_VERSION_SIZE 20
#define DIAGNOSTIC_REPORTER_MODULE_NAME_SIZE 32
#define DIAGNOSTIC_REPORTER_ERROR_CODE_SIZE 32
#define DIAGNOSTIC_REPORTER_EVENT_TYPE_SIZE 32
#define DIAGNOSTIC_REPORTER_DETAIL_CODE_SIZE 64
#define DIAGNOSTIC_REPORTER_CRASH_CAUSE_SIZE 24
#define DIAGNOSTIC_REPORTER_CRASH_FAULT_KIND_SIZE 16
#define DIAGNOSTIC_REPORTER_CRASH_TASK_NAME_SIZE 32

/*
 * Symbolic crash causes and fault kinds, mirrored by the platform constants.
 * Crash records are bounded, redacted signatures derived from the reset
 * reason; raw memory, registers, backtrace addresses, audio, images,
 * credentials, and child data are never recorded or transmitted.
 */
#define DIAGNOSTIC_REPORTER_CRASH_CAUSE_PANIC "panic"
#define DIAGNOSTIC_REPORTER_CRASH_CAUSE_WATCHDOG "watchdog"
#define DIAGNOSTIC_REPORTER_CRASH_CAUSE_TASK_WATCHDOG "task_watchdog"
#define DIAGNOSTIC_REPORTER_CRASH_CAUSE_INTERRUPT_WATCHDOG "interrupt_watchdog"
#define DIAGNOSTIC_REPORTER_CRASH_CAUSE_BROWNOUT "brownout"
#define DIAGNOSTIC_REPORTER_CRASH_CAUSE_STACK_OVERFLOW "stack_overflow"
#define DIAGNOSTIC_REPORTER_CRASH_CAUSE_HEAP_CORRUPTION "heap_corruption"
#define DIAGNOSTIC_REPORTER_CRASH_CAUSE_ASSERT "assert"
#define DIAGNOSTIC_REPORTER_CRASH_CAUSE_UNKNOWN "unknown"

#define DIAGNOSTIC_REPORTER_CRASH_FAULT_KIND_EXCEPTION "exception"
#define DIAGNOSTIC_REPORTER_CRASH_FAULT_KIND_WATCHDOG "watchdog"
#define DIAGNOSTIC_REPORTER_CRASH_FAULT_KIND_POWER_LOSS "power_loss"
#define DIAGNOSTIC_REPORTER_CRASH_FAULT_KIND_MEMORY "memory"
#define DIAGNOSTIC_REPORTER_CRASH_FAULT_KIND_SOFTWARE "software"

/** Bounded contract limits shared with the platform validator. */
#define DIAGNOSTIC_REPORTER_CRASH_FRAME_COUNT_MAX 64u
#define DIAGNOSTIC_REPORTER_CRASH_CORE_ID_MAX 3u

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

/** One bounded user-visible interaction that remains pending until accepted. */
typedef struct {
    uint32_t sequence;
    uint32_t duration_ms;
    char event_id[DIAGNOSTIC_REPORTER_EVENT_ID_SIZE];
    char event_type[DIAGNOSTIC_REPORTER_EVENT_TYPE_SIZE];
    char detail_code[DIAGNOSTIC_REPORTER_DETAIL_CODE_SIZE];
    char firmware_version[DIAGNOSTIC_REPORTER_FIRMWARE_VERSION_SIZE];
} diagnostic_reporter_interaction_event_t;

/** One redacted, bounded crash signature that remains pending until accepted. */
typedef struct {
    uint32_t sequence;
    uint32_t program_counter;
    uint32_t frame_count;
    uint32_t core_id;
    char event_id[DIAGNOSTIC_REPORTER_EVENT_ID_SIZE];
    char cause[DIAGNOSTIC_REPORTER_CRASH_CAUSE_SIZE];
    char fault_kind[DIAGNOSTIC_REPORTER_CRASH_FAULT_KIND_SIZE];
    char task_name[DIAGNOSTIC_REPORTER_CRASH_TASK_NAME_SIZE];
    char firmware_version[DIAGNOSTIC_REPORTER_FIRMWARE_VERSION_SIZE];
} diagnostic_reporter_crash_event_t;

/** One bounded diagnostic snapshot suitable for inclusion in a heartbeat. */
typedef struct {
    size_t boot_event_count;
    diagnostic_reporter_boot_event_t
        boot_events[DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY];
    size_t recovery_event_count;
    diagnostic_reporter_recovery_event_t
        recovery_events[DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY];
    size_t interaction_event_count;
    diagnostic_reporter_interaction_event_t
        interaction_events[DIAGNOSTIC_REPORTER_INTERACTION_EVENT_CAPACITY];
    size_t crash_event_count;
    diagnostic_reporter_crash_event_t
        crash_events[DIAGNOSTIC_REPORTER_CRASH_EVENT_CAPACITY];
    uint32_t newest_sequence;
    uint32_t dropped_boot_events;
    bool has_failure;
    diagnostic_reporter_failure_t failure;
} diagnostic_reporter_snapshot_t;

/**
 * @brief Initialize persistent diagnostic state and record this boot.
 *
 * Requires config_store, error_recovery, and version_info to be initialized.
 * A corrupt state record is replaced with a clean bounded record. A known
 * older layout is migrated in place so pending boot, failure, and recovery
 * history survives a firmware upgrade.
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

/**
 * @brief Record one bounded user-visible interaction event.
 *
 * event_type must be one of the interaction event types accepted by the
 * platform contract. detail_code must match the platform's stable symbolic
 * identifier contract; it must never contain free text, audio, credentials,
 * or child data. duration_ms is the real elapsed duration and must be zero
 * for instantaneous events such as a wake detection or indicator change.
 */
esp_err_t diagnostic_reporter_record_interaction(
    const char *event_type,
    const char *detail_code,
    uint32_t duration_ms
);

/**
 * @brief Record one redacted, bounded crash signature.
 *
 * cause and fault_kind must be one of the platform crash contract strings.
 * task_name must match the symbolic identifier contract and never contain
 * free text, audio, credentials, or child data. frame_count is clamped to
 * DIAGNOSTIC_REPORTER_CRASH_FRAME_COUNT_MAX and core_id to
 * DIAGNOSTIC_REPORTER_CRASH_CORE_ID_MAX. Raw memory, registers, and full
 * backtraces are never accepted.
 */
esp_err_t diagnostic_reporter_record_crash(
    const char *cause,
    const char *fault_kind,
    const char *task_name,
    uint32_t program_counter,
    uint32_t frame_count,
    uint32_t core_id
);

/** @brief Release runtime resources owned by the module. */
void diagnostic_reporter_shutdown(void);

/** @brief Return the removable-module descriptor for diagnostic_reporter. */
const module_descriptor_t *diagnostic_reporter_module_descriptor(void);

#ifdef __cplusplus
}
#endif
