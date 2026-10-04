#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bounded text size for a removable module name. */
#define ERROR_RECOVERY_MODULE_NAME_SIZE 32
/** Maximum pending module recovery transitions retained in RAM. */
#define ERROR_RECOVERY_RECOVERY_EVENT_CAPACITY 8

/** @brief Latest recoverable failure reported by a removable module. */
typedef struct {
    char module_name[ERROR_RECOVERY_MODULE_NAME_SIZE];
    esp_err_t error_code;
    unsigned int failure_count;
    unsigned int transition_count;
} error_recovery_snapshot_t;

/** @brief One module recovery transition retained by the recovery module. */
typedef struct {
    char module_name[ERROR_RECOVERY_MODULE_NAME_SIZE];
    unsigned int transition_count;
} error_recovery_recovery_t;

/**
 * @brief Bounded snapshot of module recovery transitions for diagnostics.
 *
 * The queue keeps the newest transitions. `latest_transition_count` is
 * monotonic within one boot and is shared with `error_recovery_snapshot_t`,
 * so consumers can order failures and recoveries even when both occurred
 * before the next heartbeat.
 */
typedef struct {
    size_t event_count;
    unsigned int latest_transition_count;
    error_recovery_recovery_t
        events[ERROR_RECOVERY_RECOVERY_EVENT_CAPACITY];
} error_recovery_recovery_snapshot_t;

/**
 * @brief Record a recoverable failure without restarting the device.
 *
 * The snapshot stores only the module name, error code, counters, and the
 * transition index used to order diagnostic events. A new failure starts a
 * new recovery cycle.
 */
esp_err_t error_recovery_record(const char *module_name, esp_err_t error_code);

/**
 * @brief Record that a previously failing module initialized successfully.
 *
 * Repeated calls for the same module without an intervening failure are
 * idempotent. The transition is appended to the bounded recovery queue and
 * is exposed to diagnostic_reporter.
 */
esp_err_t error_recovery_mark_recovered(const char *module_name);

/**
 * @brief Enter a controlled restart after an unrecoverable failure.
 *
 * The function never returns. Callers must first persist only non-sensitive
 * diagnostic state.
 */
void error_recovery_restart(const char *module_name, esp_err_t error_code);

/** @brief Return the most recent failure snapshot. */
error_recovery_snapshot_t error_recovery_get_snapshot(void);

/** @brief Return the bounded queue of module recovery transitions. */
error_recovery_recovery_snapshot_t error_recovery_get_recoveries(void);

/** @brief Return the removable-module descriptor for error_recovery. */
const module_descriptor_t *error_recovery_module_descriptor(void);

#ifdef __cplusplus
}
#endif
