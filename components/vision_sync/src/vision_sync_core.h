#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "camera_vision.h"
#include "vision_sync.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure helpers, free of ESP-IDF dependencies so the validation rules are
 * covered by host tests. */

/** @brief Platform task status accepted by the device contract. */
typedef enum {
    VISION_SYNC_TASK_PENDING = 0,
    VISION_SYNC_TASK_DISPATCHED,
} vision_sync_task_status_t;

/** @brief One bounded pending task parsed from the platform response. */
typedef struct {
    char task_id[64];
    camera_vision_task_type_t task_type;
    char prompt[501];
    vision_sync_task_status_t status;
} vision_sync_task_t;

/**
 * @brief Copy a JSON field into a bounded destination.
 *
 * Returns false when the value is missing, empty or does not fit, so a
 * malformed platform response is rejected instead of truncated into a
 * different task identifier.
 */
bool vision_sync_core_copy_field(
    char *destination,
    size_t destination_size,
    const char *value
);

/** @brief Return true when a platform task status is readable by the device. */
bool vision_sync_core_status_from_name(
    const char *name,
    vision_sync_task_status_t *status_out
);

/**
 * @brief Validate one provider result before it is encoded.
 *
 * Out-of-range frame metadata, oversized or unsafe labels and a confidence
 * above the contract maximum are rejected so the platform never receives a
 * malformed result payload. A capture task may legitimately carry no labels.
 */
bool vision_sync_core_result_is_valid(const vision_sync_result_t *result);

/** @brief Stable failure code reported when no provider can run the task. */
const char *vision_sync_failure_code_for_error(esp_err_t error);

#ifdef __cplusplus
}
#endif
