#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "camera_vision.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure helpers, free of ESP-IDF dependencies so the encoding and validation
 * rules are covered by host tests. */

/** @brief Return true when a task id matches the shared contract. */
bool camera_vision_core_task_id_is_valid(const char *task_id);

/** @brief Return true when one label is bounded and well formed. */
bool camera_vision_core_label_is_valid(const char *label);

/** @brief Return the canonical text for one task type, or NULL. */
const char *camera_vision_core_task_type_name(
    camera_vision_task_type_t task_type
);

/**
 * @brief Encode a bounded JSON result.
 *
 * Returns the encoded length, or a negative value such as
 * -CAMERA_VISION_ERR_TOO_MANY_LABELS / -CAMERA_VISION_ERR_INVALID_LABEL /
 * -CAMERA_VISION_ERR_BUSY when the input is invalid or the buffer is small.
 */
int camera_vision_core_encode_result(
    const char *task_id,
    const camera_vision_label_t *labels,
    size_t label_count,
    const char *summary,
    char *output,
    size_t output_size
);

#ifdef __cplusplus
}
#endif
