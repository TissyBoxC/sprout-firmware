#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "content_library.h"
#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable package manager error codes. */
typedef enum {
    CONTENT_PACKAGE_OK = 0,
    CONTENT_PACKAGE_ERR_NOT_INITIALIZED,
    CONTENT_PACKAGE_ERR_INVALID_ARGUMENT,
    CONTENT_PACKAGE_ERR_OFFLINE,
    CONTENT_PACKAGE_ERR_UNAUTHENTICATED,
    CONTENT_PACKAGE_ERR_MANIFEST,
    CONTENT_PACKAGE_ERR_STORAGE,
    CONTENT_PACKAGE_ERR_DOWNLOAD,
    CONTENT_PACKAGE_ERR_PAUSED,
    CONTENT_PACKAGE_ERR_NOT_FOUND,
} content_package_error_t;

/** @brief Overall synchronisation state for the device UI. */
typedef enum {
    CONTENT_PACKAGE_STATE_IDLE = 0,
    CONTENT_PACKAGE_STATE_SYNCING_MANIFEST,
    CONTENT_PACKAGE_STATE_DOWNLOADING,
    CONTENT_PACKAGE_STATE_PAUSED,
    CONTENT_PACKAGE_STATE_FAILED,
    CONTENT_PACKAGE_STATE_COMPLETE,
} content_package_state_t;

/** @brief Bounded manager progress snapshot. */
typedef struct {
    content_package_state_t state;
    content_package_error_t last_error;
    int64_t catalog_revision;
    size_t indexed_packages;
    size_t ready_packages;
    size_t pending_downloads;
    char active_package_id[CONTENT_LIBRARY_ID_SIZE];
    uint64_t active_received_bytes;
    uint64_t active_total_bytes;
} content_package_snapshot_t;

/** @brief Initialize the manifest worker and local index linkage. */
esp_err_t content_package_manager_init(void);

/** @brief Return true when the manager is ready. */
bool content_package_manager_is_ready(void);

/** @brief Start or resume manifest synchronisation. */
esp_err_t content_package_manager_start(void);

/** @brief Pause new downloads while preserving partial files. */
esp_err_t content_package_manager_pause(void);

/** @brief Resume downloads after a pause. */
esp_err_t content_package_manager_resume(void);

/**
 * @brief Trigger one manifest sync immediately.
 *
 * Returns CONTENT_PACKAGE_ERR_OFFLINE when Wi-Fi is disconnected and
 * CONTENT_PACKAGE_ERR_UNAUTHENTICATED when the device session is absent.
 */
esp_err_t content_package_manager_sync_now(void);

/** @brief Copy the current manager snapshot. */
esp_err_t content_package_manager_get_snapshot(
    content_package_snapshot_t *snapshot_out
);

/** @brief Return the stable string for one manager state. */
const char *content_package_state_name(content_package_state_t state);

/** @brief Return the stable string for one manager error code. */
const char *content_package_error_name(content_package_error_t error);

/** @brief Return the removable-module descriptor for content_package_manager. */
const module_descriptor_t *content_package_manager_module_descriptor(void);

#ifdef __cplusplus
}
#endif
