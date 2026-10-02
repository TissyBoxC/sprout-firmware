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
