#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Supported on-device vision task types, mirrored by the platform. */
typedef enum {
    CAMERA_VISION_TASK_OBJECT_RECOGNITION = 0,
    CAMERA_VISION_TASK_PICTURE_BOOK_RECOGNITION,
    CAMERA_VISION_TASK_PHOTO_QA,
    CAMERA_VISION_TASK_CAPTURE,
    CAMERA_VISION_TASK_COUNT,
} camera_vision_task_type_t;

/** @brief Bounded task lifecycle states. */
typedef enum {
    CAMERA_VISION_STATE_IDLE = 0,
    CAMERA_VISION_STATE_READY,
    CAMERA_VISION_STATE_ACKNOWLEDGED,
    CAMERA_VISION_STATE_ERROR,
} camera_vision_state_t;

/** @brief Stable errors returned by the vision pipeline. */
typedef enum {
    CAMERA_VISION_OK = 0,
    CAMERA_VISION_ERR_NULL_ARGUMENT,
    CAMERA_VISION_ERR_NOT_READY,
    CAMERA_VISION_ERR_BUSY,
    CAMERA_VISION_ERR_UNSUPPORTED,
    CAMERA_VISION_ERR_PRIVACY_CONSENT_REQUIRED,
    CAMERA_VISION_ERR_INVALID_LABEL,
    CAMERA_VISION_ERR_TOO_MANY_LABELS,
} camera_vision_error_t;

/** @brief Maximum labels reported for one result, mirrored by the platform. */
#define CAMERA_VISION_MAX_LABELS 16u
/** @brief Maximum runes in a single label. */
#define CAMERA_VISION_MAX_LABEL_RUNES 64u
/** @brief Integer confidence scale; 10000 means 100 percent. */
#define CAMERA_VISION_MAX_CONFIDENCE 10000u

/** @brief One recognized label with an integer confidence score. */
typedef struct {
    char label[CAMERA_VISION_MAX_LABEL_RUNES + 1];
    uint32_t confidence;
} camera_vision_label_t;

/** @brief Bounded snapshot for diagnostics and the platform result. */
typedef struct {
    char task_id[64];
    camera_vision_task_type_t task_type;
    camera_vision_state_t state;
    uint32_t sequence;
    uint32_t failure_count;
} camera_vision_snapshot_t;

/** @brief Initialize the vision task pipeline. */
esp_err_t camera_vision_init(void);

/** @brief Return true after a successful initialization. */
bool camera_vision_is_ready(void);

/** @brief Return the current bounded snapshot. */
camera_vision_snapshot_t camera_vision_get_snapshot(void);

/**
 * @brief Adopt one task delivered by the platform.
 *
 * The task id is bounded and copied defensively; an unsupported type or an
 * invalid id is rejected so a malformed platform response cannot corrupt the
 * pipeline.
 */
camera_vision_error_t camera_vision_accept_task(
    const char *task_id,
    camera_vision_task_type_t task_type
);

/** @brief Acknowledge the accepted task back to the platform. */
camera_vision_error_t camera_vision_acknowledge(void);

/**
 * @brief Encode a bounded result payload into the caller's buffer.
 *
 * Writes JSON containing the task id, recognized labels, and summary. Returns
 * the number of bytes written excluding the null terminator, or a negative
 * camera_vision_error_t value. The buffer must be large enough; a truncation
 * is reported as CAMERA_VISION_ERR_BUSY so the caller can retry with a larger
 * buffer.
 */
int camera_vision_encode_result(
    const camera_vision_label_t *labels,
    size_t label_count,
    const char *summary,
    char *output,
    size_t output_size
);

/** @brief Record a bounded failure and return to ready. */
camera_vision_error_t camera_vision_fail(const char *failure_code);

/** @brief Mark the accepted task complete and return to ready. */
camera_vision_error_t camera_vision_complete(void);

/** @brief Return the stable contract name for one task type. */
const char *camera_vision_task_type_name(camera_vision_task_type_t task_type);

/** @brief Resolve a contract name to a task type; false when unknown. */
bool camera_vision_task_type_from_name(
    const char *name,
    camera_vision_task_type_t *task_type_out
);

/** @brief Return the stable text for one error. */
const char *camera_vision_error_name(camera_vision_error_t error);

/** @brief Return the removable-module descriptor for camera_vision. */
const module_descriptor_t *camera_vision_module_descriptor(void);

#ifdef __cplusplus
}
#endif
