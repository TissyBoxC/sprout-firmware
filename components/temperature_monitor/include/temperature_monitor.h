#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Normalized thermal band shared with the runtime status contract. */
typedef enum {
    TEMPERATURE_STATE_UNKNOWN = 0,
    TEMPERATURE_STATE_NORMAL,
    TEMPERATURE_STATE_WARM,
    TEMPERATURE_STATE_HOT,
    TEMPERATURE_STATE_CRITICAL,
} temperature_state_t;

/** @brief One bounded thermal snapshot suitable for a heartbeat payload. */
typedef struct {
    temperature_state_t state;
    /* Reading in tenths of a degree Celsius. Valid only when has_measurement. */
    int celsius_tenths;
    bool has_measurement;
} temperature_snapshot_t;

/**
 * @brief Initialize periodic thermal measurement.
 *
 * The module starts with an unknown snapshot until the first successful
 * reading. Boards without an internal temperature sensor keep reporting
 * 'unknown' rather than failing initialization.
 */
esp_err_t temperature_monitor_init(void);

/** @brief Return the latest bounded thermal snapshot. */
temperature_snapshot_t temperature_monitor_get_snapshot(void);

/** @brief Return the stable contract string for one thermal band. */
const char *temperature_monitor_state_name(temperature_state_t state);

/** @brief Return the removable-module descriptor for temperature_monitor. */
const module_descriptor_t *temperature_monitor_module_descriptor(void);

#ifdef __cplusplus
}
#endif
