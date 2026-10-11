#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable image-privacy schema version shared with the platform. */
#define IMAGE_PRIVACY_SCHEMA_VERSION 1u

/** @brief Approved bounds, mirroring the platform contract exactly. */
#define IMAGE_PRIVACY_MIN_RETENTION_DAYS 0u
#define IMAGE_PRIVACY_MAX_RETENTION_DAYS 30u
#define IMAGE_PRIVACY_MAX_IMAGE_COUNT 1000000u

/** @brief The fixed processing-mode catalog shared with the platform. */
typedef enum {
    IMAGE_PROCESSING_OFF = 0,
    IMAGE_PROCESSING_METADATA,
    IMAGE_PROCESSING_LOCAL_FULL,
} image_processing_mode_t;

/** @brief The fixed upload-scope catalog shared with the platform. */
typedef enum {
    IMAGE_UPLOAD_NONE = 0,
    IMAGE_UPLOAD_THUMBNAILS,
    IMAGE_UPLOAD_ANALYSIS,
} image_upload_scope_t;

/** @brief The fixed redaction-level catalog shared with the platform. */
typedef enum {
    IMAGE_REDACTION_NONE = 0,
    IMAGE_REDACTION_STANDARD,
    IMAGE_REDACTION_STRICT,
} image_redaction_level_t;

/** @brief The stable block reasons shared with the platform. */
typedef enum {
    IMAGE_BLOCK_NONE = 0,
    IMAGE_BLOCK_DISABLED,
    IMAGE_BLOCK_CONSENT,
    IMAGE_BLOCK_SCOPE,
    IMAGE_BLOCK_QUOTA,
    IMAGE_BLOCK_ERROR,
} image_block_reason_t;

/**
 * @brief One bounded image-privacy policy for a family.
 *
 * The struct mirrors the platform policy field for field. The feature and
 * upload default off and local-only processing defaults on, so a device never
 * uploads a frame without an explicit guardian decision.
 */
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t upload_allowed;
    uint8_t local_processing_only;
    uint8_t processing_mode;
    uint8_t upload_scope;
    uint8_t retention_days;
    uint8_t redaction_level;
    uint8_t require_child_consent;
    uint8_t reserved;
} image_privacy_policy_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    IMAGE_PRIVACY_OK = 0,
    IMAGE_PRIVACY_ERR_NULL_ARGUMENT,
    IMAGE_PRIVACY_ERR_INVALID_POLICY,
    IMAGE_PRIVACY_ERR_INVALID_STATE,
    IMAGE_PRIVACY_ERR_PERMISSION_DENIED,
    IMAGE_PRIVACY_ERR_STORAGE,
} image_privacy_error_t;

/** @brief Return the conservative default policy (processing and upload off). */
image_privacy_policy_t image_privacy_default(void);

/** @brief Clamp one policy into the documented bounds. */
image_privacy_policy_t image_privacy_clamp(
    const image_privacy_policy_t *policy
);

/** @brief Return true when a policy is version-compatible and bounded. */
bool image_privacy_is_valid(const image_privacy_policy_t *policy);

/** @brief Return true when the policy permits local image processing. */
bool image_privacy_can_process(const image_privacy_policy_t *policy);

/** @brief Return true when the policy permits image upload. */
bool image_privacy_can_upload(const image_privacy_policy_t *policy);

/** @brief Return true when a processing-mode value is in the catalog. */
bool image_privacy_is_valid_mode(image_processing_mode_t mode);

/** @brief Return true when an upload-scope value is in the catalog. */
bool image_privacy_is_valid_scope(image_upload_scope_t scope);

/** @brief Return true when a redaction-level value is in the catalog. */
bool image_privacy_is_valid_redaction(image_redaction_level_t level);

/** @brief Return true when a block-reason value is in the catalog. */
bool image_privacy_is_valid_block_reason(image_block_reason_t reason);

/** @brief Return the stable string for one image-privacy error code. */
const char *image_privacy_error_name(image_privacy_error_t error);

/** @brief Return the stable string for one processing mode. */
const char *image_privacy_mode_name(image_processing_mode_t mode);

/** @brief Return the stable string for one upload scope. */
const char *image_privacy_scope_name(image_upload_scope_t scope);

/** @brief Return the stable string for one redaction level. */
const char *image_privacy_redaction_name(image_redaction_level_t level);

/** @brief Return the stable string for one block reason. */
const char *image_privacy_block_reason_name(image_block_reason_t reason);

/** @brief Initialize the image-privacy store and pipeline state. */
esp_err_t image_privacy_init(void);

/** @brief Return true when the image-privacy store is ready. */
bool image_privacy_is_ready(void);

/** @brief Store a guardian policy, clamping out-of-range values. */
esp_err_t image_privacy_set(const image_privacy_policy_t *policy);

/** @brief Return the currently applied policy. */
image_privacy_policy_t image_privacy_get(void);

/** @brief Apply a policy that is already version-compatible and bounded. */
esp_err_t image_privacy_apply(const image_privacy_policy_t *policy);

/** @brief Return true when the current policy permits local processing. */
bool image_privacy_processing_allowed(void);

/**
 * @brief Begin processing one frame under the applied policy.
 *
 * Processing is refused unless the feature is enabled and the mode is not off.
 * A refusal records a bounded block reason so the next status report can
 * explain why the pipeline stopped.
 */
esp_err_t image_privacy_begin_processing(void);

/**
 * @brief Authorize an upload of the current processed frame.
 *
 * The upload is refused unless upload is enabled, the policy is not local-only
 * and the upload scope is not none. A refusal records a bounded block reason.
 */
esp_err_t image_privacy_authorize_upload(image_upload_scope_t scope);

/** @brief Record one successfully uploaded frame. */
esp_err_t image_privacy_record_upload(void);

/** @brief Record one bounded pipeline block reason. */
esp_err_t image_privacy_block(image_block_reason_t reason);

/** @brief Reset the session-local processed/uploaded/blocked counters. */
esp_err_t image_privacy_reset_counters(void);

/** @brief Return how many processed frames were counted this session. */
uint32_t image_privacy_processed_count(void);

/** @brief Return how many uploaded frames were counted this session. */
uint32_t image_privacy_uploaded_count(void);

/** @brief Return how many blocked frames were counted this session. */
uint32_t image_privacy_blocked_count(void);

/** @brief Return the reason the last processing or upload was blocked. */
image_block_reason_t image_privacy_last_block_reason(void);

/** @brief Return the removable-module descriptor for image_privacy. */
const module_descriptor_t *image_privacy_module_descriptor(void);

#ifdef __cplusplus
}
#endif
