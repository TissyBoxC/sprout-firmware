#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Offline state exposed to the runtime heartbeat contract. */
typedef enum {
    OFFLINE_FALLBACK_STATE_ONLINE = 0,
    OFFLINE_FALLBACK_STATE_GRACE,
    OFFLINE_FALLBACK_STATE_OFFLINE,
} offline_fallback_state_t;

/** @brief Reason for the current fallback state. */
typedef enum {
    OFFLINE_REASON_NONE = 0,
    OFFLINE_REASON_NETWORK_UNAVAILABLE,
    OFFLINE_REASON_AUTHENTICATION_FAILED,
    OFFLINE_REASON_TIME_NOT_SYNCHRONIZED,
    OFFLINE_REASON_SERVICE_UNAVAILABLE,
} offline_reason_t;

/** @brief One bounded fallback snapshot. */
typedef struct {
    offline_fallback_state_t state;
    offline_reason_t reason;
    bool fallback_active;
    uint16_t pending_telemetry;
} offline_fallback_snapshot_t;

/**
 * @brief Initialize offline state tracking.
 *
 * The device starts in the online state until a network event proves
 * otherwise.
 */
esp_err_t offline_fallback_init(void);

/** @brief Return the current bounded fallback snapshot. */
offline_fallback_snapshot_t offline_fallback_get_snapshot(void);

/** @brief Mark one telemetry item as pending while offline. */
esp_err_t offline_fallback_record_pending_telemetry(void);

/** @brief Clear pending telemetry after a successful recovery. */
esp_err_t offline_fallback_clear_pending_telemetry(void);

/** @brief Enter fallback because an authenticated platform call failed. */
esp_err_t offline_fallback_mark_service_unavailable(void);

/** @brief Enter fallback because the clock is not trusted. */
esp_err_t offline_fallback_mark_time_unsynchronized(void);

/** @brief Leave fallback after network and authentication recover. */
esp_err_t offline_fallback_mark_recovered(void);

/** @brief Return the removable-module descriptor for offline_fallback. */
const module_descriptor_t *offline_fallback_module_descriptor(void);

#ifdef __cplusplus
}
#endif
