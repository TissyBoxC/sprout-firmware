#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "camera_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure capture state machine, free of ESP-IDF dependencies so the lifecycle
 * rules are covered by host tests. */

/** @brief Validate one frame against the shared contract bounds. */
bool camera_capture_core_frame_is_valid(
    uint32_t width,
    uint32_t height,
    uint32_t byte_size
);

/** @brief Return true when a start is allowed from the current state. */
bool camera_capture_core_can_start(camera_capture_state_t state);

/** @brief Return true when a finish is allowed from the current state. */
bool camera_capture_core_can_finish(camera_capture_state_t state);

/** @brief Apply a successful capture to a mutable snapshot. */
void camera_capture_core_apply_frame(
    camera_capture_snapshot_t *snapshot,
    uint32_t width,
    uint32_t height,
    uint32_t byte_size
);

/** @brief Record one dropped frame and return to ready. */
void camera_capture_core_record_drop(camera_capture_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
