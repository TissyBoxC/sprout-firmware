#pragma once

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Last recoverable failure reported by a removable module. */
typedef struct {
    const char *module_name;
    esp_err_t error_code;
    unsigned int failure_count;
} error_recovery_snapshot_t;

/**
 * @brief Record a recoverable failure without restarting the device.
 *
 * The snapshot stores only the module name and error code.
 */
esp_err_t error_recovery_record(const char *module_name, esp_err_t error_code);

/**
 * @brief Enter a controlled restart after an unrecoverable failure.
 *
 * The function never returns. Callers must first persist only non-sensitive
 * diagnostic state.
 */
void error_recovery_restart(const char *module_name, esp_err_t error_code);

/** @brief Return the most recent failure snapshot. */
error_recovery_snapshot_t error_recovery_get_snapshot(void);

/** @brief Return the removable-module descriptor for error_recovery. */
const module_descriptor_t *error_recovery_module_descriptor(void);

#ifdef __cplusplus
}
#endif
