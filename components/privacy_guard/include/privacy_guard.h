#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sensitive data classes governed by guardian consent. */
typedef enum {
    PRIVACY_GUARD_DATA_AUDIO_UPLOAD = 0,
    PRIVACY_GUARD_DATA_IMAGE_UPLOAD,
    PRIVACY_GUARD_DATA_CONVERSATION_HISTORY,
    PRIVACY_GUARD_DATA_USAGE_ANALYTICS,
    PRIVACY_GUARD_DATA_COUNT,
} privacy_guard_data_class_t;

/** @brief Reasons a local privacy action was requested. */
typedef enum {
    PRIVACY_GUARD_ACTION_GUARDIAN_REQUEST = 0,
    PRIVACY_GUARD_ACTION_UNBIND,
    PRIVACY_GUARD_ACTION_FACTORY_RESET,
    PRIVACY_GUARD_ACTION_DELETE_LOCAL_DATA,
    PRIVACY_GUARD_ACTION_EXTERNAL_REQUEST,
} privacy_guard_action_t;

/** @brief Stable errors and decisions returned by the guard. */
typedef enum {
    PRIVACY_GUARD_OK = 0,
    PRIVACY_GUARD_ERR_NULL_ARGUMENT,
    PRIVACY_GUARD_ERR_NOT_INITIALIZED,
    PRIVACY_GUARD_ERR_INVALID_CLASS,
    PRIVACY_GUARD_ERR_STORAGE,
    PRIVACY_GUARD_ERR_CONSENT_REQUIRED,
    PRIVACY_GUARD_ERR_CONSENT_REVOKED,
    PRIVACY_GUARD_ERR_CACHE_BUSY,
} privacy_guard_error_t;

/** @brief One data class decision returned to capture and upload paths. */
typedef struct {
    privacy_guard_data_class_t data_class;
    bool allowed;
    bool guardian_consent;
    bool policy_requires_consent;
    privacy_guard_error_t reason;
} privacy_guard_decision_t;

/** @brief Bounded privacy state for diagnostics and the parent application. */
typedef struct {
    bool is_initialized;
    bool audio_upload_consent;
    bool image_upload_consent;
    bool conversation_history_consent;
    bool usage_analytics_consent;
    uint32_t local_deletion_count;
    uint32_t consent_revision;
    privacy_guard_error_t last_error;
} privacy_guard_snapshot_t;

/**
 * @brief Persistence interface used by the guard.
 *
 * Host tests provide an in-memory implementation; production uses NVS through
 * config_store. The interface stores only consent booleans and a revision
 * counter; it never stores audio, images, or conversation content.
 */
typedef esp_err_t (*privacy_guard_load_fn)(
    bool *audio_upload_consent,
    bool *image_upload_consent,
    bool *conversation_history_consent,
    bool *usage_analytics_consent,
    uint32_t *consent_revision
);
typedef esp_err_t (*privacy_guard_save_fn)(
    bool audio_upload_consent,
    bool image_upload_consent,
    bool conversation_history_consent,
    bool usage_analytics_consent,
    uint32_t consent_revision
);
typedef esp_err_t (*privacy_guard_clear_cache_fn)(
    privacy_guard_action_t action
);

typedef struct {
    privacy_guard_load_fn load;
    privacy_guard_save_fn save;
    privacy_guard_clear_cache_fn clear_cache;
} privacy_guard_storage_t;

/**
 * @brief Initialize the guard with the production NVS-backed storage.
 *
 * Defaults are deny for audio upload, image upload, conversation history, and
 * usage analytics. Nothing is uploaded before a guardian explicitly grants
 * consent, and a missing or corrupt record is treated as no consent.
 */
esp_err_t privacy_guard_init(void);

/** @brief Initialize with an explicit storage interface for tests. */
esp_err_t privacy_guard_init_with_storage(
    const privacy_guard_storage_t *storage
);

/** @brief Return true after initialization. */
bool privacy_guard_is_ready(void);

/** @brief Read the current bounded privacy snapshot. */
privacy_guard_snapshot_t privacy_guard_get_snapshot(void);

/** @brief Return true when guardian consent exists for one data class. */
bool privacy_guard_has_consent(privacy_guard_data_class_t data_class);

/**
 * @brief Evaluate one capture or upload action.
 *
 * Audio and image uploads are denied unless the guardian has granted consent
 * for that class. Conversation history and usage analytics follow the same
 * rule. The decision always carries a stable reason.
 */
privacy_guard_error_t privacy_guard_evaluate(
    privacy_guard_data_class_t data_class,
    privacy_guard_decision_t *decision_out
);

/** @brief Grant guardian consent for one data class. */
privacy_guard_error_t privacy_guard_grant_consent(
    privacy_guard_data_class_t data_class
);

/** @brief Revoke guardian consent for one data class. */
privacy_guard_error_t privacy_guard_revoke_consent(
    privacy_guard_data_class_t data_class
);

/** @brief Revoke every optional data class at once. */
privacy_guard_error_t privacy_guard_revoke_all(void);

/**
 * @brief Clear local sensitive caches for an unbind, reset, or guardian action.
 *
 * The registered clear-cache callback must remove audio, image, and
 * conversation caches. A callback failure is surfaced and the deletion
 * counter is not advanced, so the caller can retry or enter safe mode.
 */
privacy_guard_error_t privacy_guard_clear_local_data(
    privacy_guard_action_t action
);

/** @brief Return the number of completed local cache deletions. */
uint32_t privacy_guard_local_deletion_count(void);

/** @brief Return the stable name for one data class. */
const char *privacy_guard_data_class_name(
    privacy_guard_data_class_t data_class
);

/** @brief Return the stable text for one guard error. */
const char *privacy_guard_error_name(privacy_guard_error_t error);

/** @brief Return the removable-module descriptor for privacy_guard. */
const module_descriptor_t *privacy_guard_module_descriptor(void);

#ifdef __cplusplus
}
#endif
