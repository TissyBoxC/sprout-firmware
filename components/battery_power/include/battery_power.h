#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable power policy schema version shared with the platform. */
#define BATTERY_SCHEMA_VERSION 1u

/** @brief Bounds for the low-battery warning threshold in percent. */
#define BATTERY_MIN_WARN_PERCENT 5u
#define BATTERY_MAX_WARN_PERCENT 50u

/** @brief Bounds for the deep-sleep idle window in seconds. */
#define BATTERY_MIN_SLEEP_AFTER_SECONDS 60u
#define BATTERY_MAX_SLEEP_AFTER_SECONDS 86400u

/** @brief Bounds for the low-power entry threshold in percent. */
#define BATTERY_MIN_LOW_POWER_PERCENT 5u
#define BATTERY_MAX_LOW_POWER_PERCENT 50u

/** @brief Normalized charge state shared with the runtime status contract. */
typedef enum {
    BATTERY_CHARGE_UNKNOWN = 0,
    BATTERY_CHARGE_DISCHARGING,
    BATTERY_CHARGE_CHARGING,
    BATTERY_CHARGE_FULL,
} battery_charge_state_t;

/** @brief Normalized power mode the firmware is currently applying. */
typedef enum {
    BATTERY_MODE_NORMAL = 0,
    BATTERY_MODE_LOW_POWER,
    BATTERY_MODE_DEEP_SLEEP,
} battery_power_mode_t;

/** @brief One validated battery and low-power policy. */
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t low_power_enabled;
    uint8_t deep_sleep_enabled;
    uint8_t charge_priority;
    uint8_t low_battery_warn_percent;
    uint8_t low_power_enter_percent;
    uint32_t sleep_after_seconds;
} battery_policy_t;

/** @brief One bounded battery snapshot suitable for a heartbeat payload. */
typedef struct {
    battery_charge_state_t charge_state;
    uint8_t percent;
    int16_t millivolts;
    bool has_measurement;
} battery_snapshot_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    BATTERY_OK = 0,
    BATTERY_ERR_NULL_ARGUMENT,
    BATTERY_ERR_INVALID_POLICY,
    BATTERY_ERR_STORAGE,
} battery_error_t;

/** @brief Return the conservative default policy. */
battery_policy_t battery_default(void);

/** @brief Clamp one policy into the documented bounds. */
battery_policy_t battery_clamp(const battery_policy_t *policy);

/** @brief Return true when a policy is version-compatible and bounded. */
bool battery_is_valid(const battery_policy_t *policy);

/** @brief Return the stable string for one battery error code. */
const char *battery_error_name(battery_error_t error);

/** @brief Apply the cached policy to the power manager. */
esp_err_t battery_apply(const battery_policy_t *policy);

/** @brief Initialize the battery policy store. */
esp_err_t battery_init(void);

/** @brief Return true when the battery policy store is ready. */
bool battery_is_ready(void);

/** @brief Store a guardian policy, clamping out-of-range values. */
esp_err_t battery_set(const battery_policy_t *policy);

/** @brief Return the currently applied policy. */
battery_policy_t battery_get(void);

/** @brief Store the latest bounded battery snapshot. */
void battery_report_snapshot(const battery_snapshot_t *snapshot);

/** @brief Return the latest bounded battery snapshot. */
battery_snapshot_t battery_get_snapshot(void);

/** @brief Return the stable string for one charge state. */
const char *battery_charge_state_name(battery_charge_state_t state);

/** @brief Return the stable string for one power mode. */
const char *battery_power_mode_name(battery_power_mode_t mode);

/**
 * @brief Decide which power mode the policy should apply right now.
 *
 * The pure decision function encodes the low-power and deep-sleep policy so the
 * same logic runs in host tests and on the device. Charging always returns to
 * normal so a device on power never stays throttled.
 */
battery_power_mode_t battery_decide_mode(
    const battery_policy_t *policy,
    const battery_snapshot_t *snapshot,
    bool idle_elapsed
);

/** @brief Return true when the low-battery warning threshold is reached. */
bool battery_low_warning(
    const battery_policy_t *policy,
    const battery_snapshot_t *snapshot
);

/** @brief Return true when the configured wake source is retained. */
bool battery_wake_source_retained(
    const battery_policy_t *policy,
    battery_power_mode_t mode
);

/** @brief Return the removable-module descriptor for battery_power. */
const module_descriptor_t *battery_module_descriptor(void);

#ifdef __cplusplus
}
#endif
