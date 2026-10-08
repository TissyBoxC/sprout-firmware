#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Clock trust states shared with the runtime status contract. */
typedef enum {
    TIME_SYNC_STATE_UNSYNCHRONIZED = 0,
    TIME_SYNC_STATE_SYNCHRONIZING,
    TIME_SYNC_STATE_SYNCHRONIZED,
} time_sync_state_t;

/** @brief Time sources exposed to the runtime status contract. */
typedef enum {
    TIME_SYNC_SOURCE_NONE = 0,
    TIME_SYNC_SOURCE_SNTP,
    TIME_SYNC_SOURCE_PLATFORM,
} time_sync_source_t;

/** @brief One civil calendar timestamp in the device's local timezone. */
typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
} time_sync_local_time_t;

/** @brief State transition callback; never runs on an ISR. */
typedef void (*time_sync_state_callback_t)(
    time_sync_state_t state,
    time_sync_source_t source,
    void *context
);

/**
 * @brief Initialize SNTP and register a network state observer.
 *
 * The function is idempotent and does not block on DNS or network access.
 */
esp_err_t time_sync_init(void);

/** @brief Return true when the clock module has been initialized. */
bool time_sync_is_ready(void);

/** @brief Return the current clock trust state. */
time_sync_state_t time_sync_get_state(void);

/** @brief Return the source of the last successful synchronization. */
time_sync_source_t time_sync_get_source(void);

/**
 * @brief Return the Unix time of the last successful synchronization.
 *
 * Returns 0 when the device has never synchronized a trusted clock.
 */
int64_t time_sync_get_last_synced_epoch(void);

/**
 * @brief Return the measured offset in milliseconds from the last sync.
 *
 * The value is bounded to the runtime contract range.
 */
int time_sync_get_offset_ms(void);

/** @brief Return true when the system clock is trusted for TLS and OTA. */
bool time_sync_is_synchronized(void);

/**
 * @brief Return the configured device timezone offset in minutes.
 *
 * The value is applied as local_time = UTC + offset. Guangdong, the default
 * deployment region, uses +480 so the value matches Asia/Shanghai and the
 * disabled-period contract without requiring a full TZ database on device.
 */
int32_t time_sync_get_timezone_offset_minutes(void);

/**
 * @brief Convert a UTC epoch to the local civil time for one timezone offset.
 *
 * Pure function with no ESP-IDF dependency so host tests can cover disabled
 * periods, cross-midnight rollover, and DST-style fixed offsets.
 */
esp_err_t time_sync_unix_to_local_civil(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    time_sync_local_time_t *local_time_out
);

/**
 * @brief Return the local civil date as days since 1970-01-01.
 *
 * This is the canonical day key used by the usage ledger. Two UTC instants
 * that fall on the same local calendar day share the same key regardless of
 * the device's wall-clock hour.
 */
esp_err_t time_sync_local_day_key(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    int32_t *day_key_out
);

/**
 * @brief Return the local minute within the day on a 0..1439 scale.
 *
 * Returns ESP_ERR_INVALID_ARG for an out-of-range offset or NULL output.
 */
esp_err_t time_sync_local_minute_of_day(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    int32_t *minute_of_day_out
);

/**
 * @brief Restart SNTP synchronization immediately.
 *
 * Used by the runtime maintenance command. It does not erase the last trusted
 * time or weaken token validation.
 */
esp_err_t time_sync_resynchronize(void);

/**
 * @brief Notify the module that the platform supplied an authoritative time.
 *
 * Used after an authenticated platform response so a blocked SNTP server does
 * not prevent token validation, logging, OTA, or scheduled reminders.
 */
esp_err_t time_sync_accept_platform_time(int64_t unix_time_seconds);

/**
 * @brief Register the single state observer.
 *
 * Passing NULL clears the observer. The callback runs on the SNTP or network
 * event task and must not block.
 */
esp_err_t time_sync_set_state_callback(
    time_sync_state_callback_t callback,
    void *context
);

/** @brief Return the removable-module descriptor for time_sync. */
const module_descriptor_t *time_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
