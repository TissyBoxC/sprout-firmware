#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable reasons accepted by factory_reset_request. */
typedef enum {
    FACTORY_RESET_REASON_GUARDIAN_REQUEST = 0,
    FACTORY_RESET_REASON_BUTTON_GESTURE,
    FACTORY_RESET_REASON_PROVISIONING_RESET,
    FACTORY_RESET_REASON_INTERNAL_RECOVERY,
} factory_reset_reason_t;

/** @brief Stable errors returned by the factory reset module. */
typedef enum {
    FACTORY_RESET_OK = 0,
    FACTORY_RESET_ERR_NOT_INITIALIZED,
    FACTORY_RESET_ERR_INVALID_REASON,
    FACTORY_RESET_ERR_NO_PENDING_RESET,
    FACTORY_RESET_ERR_EXPIRED,
    FACTORY_RESET_ERR_STORAGE,
} factory_reset_error_t;

/** @brief Bounded factory reset state for diagnostics and the device UI. */
typedef struct {
    bool is_pending;
    factory_reset_reason_t request_reason;
    int64_t requested_at_ms;
    /** Milliseconds left before a pending request expires; zero when idle. */
    uint32_t remaining_ms;
    factory_reset_error_t last_result;
    uint32_t completed_count;
    bool is_ready;
} factory_reset_snapshot_t;

/**
 * @brief Request a factory reset without erasing anything.
 *
 * The request remains pending for CONFIG_FACTORY_RESET_PENDING_TIMEOUT_SECONDS
 * and must be confirmed explicitly. Requesting again while pending replaces
 * the reason and restarts the timeout.
 */
factory_reset_error_t factory_reset_request(factory_reset_reason_t reason);

/**
 * @brief Confirm the pending request and erase device-owned configuration.
 *
 * Only the configured NVS partition and the optional config_store credentials
 * are erased. Returns FACTORY_RESET_ERR_EXPIRED after the bounded timeout and
 * FACTORY_RESET_ERR_NO_PENDING_RESET when no unexpired request exists.
 */
factory_reset_error_t factory_reset_confirm(void);

/** @brief Cancel a pending request without erasing anything. */
factory_reset_error_t factory_reset_cancel(void);

/** @brief Return true when an unexpired request is waiting for confirmation. */
bool factory_reset_is_pending(void);

/** @brief Return a consistent bounded reset snapshot. */
factory_reset_snapshot_t factory_reset_get_snapshot(void);

/** @brief Return the stable string for one reset reason. */
const char *factory_reset_reason_name(factory_reset_reason_t reason);

/** @brief Return the stable string for one reset error code. */
const char *factory_reset_error_name(factory_reset_error_t error);

/** @brief Return the removable-module descriptor for factory_reset. */
const module_descriptor_t *factory_reset_module_descriptor(void);

#ifdef __cplusplus
}
#endif
