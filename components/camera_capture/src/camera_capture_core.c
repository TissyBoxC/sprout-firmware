#include "camera_capture_core.h"

bool camera_capture_core_frame_is_valid(
    uint32_t width,
    uint32_t height,
    uint32_t byte_size
) {
    if (width < CAMERA_CAPTURE_MIN_DIMENSION ||
        width > CAMERA_CAPTURE_MAX_DIMENSION ||
        height < CAMERA_CAPTURE_MIN_DIMENSION ||
        height > CAMERA_CAPTURE_MAX_DIMENSION) {
        return false;
    }
    return byte_size > 0u && byte_size <= CAMERA_CAPTURE_MAX_FRAME_BYTES;
}

bool camera_capture_core_can_start(camera_capture_state_t state) {
    return state == CAMERA_CAPTURE_READY;
}

bool camera_capture_core_can_finish(camera_capture_state_t state) {
    return state == CAMERA_CAPTURE_CAPTURING;
}

void camera_capture_core_apply_frame(
    camera_capture_snapshot_t *snapshot,
    uint32_t width,
    uint32_t height,
    uint32_t byte_size
) {
    if (snapshot == NULL) {
        return;
    }
    snapshot->width = width;
    snapshot->height = height;
    snapshot->byte_size = byte_size;
    snapshot->sequence += 1u;
    snapshot->state = CAMERA_CAPTURE_READY;
}

void camera_capture_core_record_drop(camera_capture_snapshot_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }
    snapshot->dropped_frames += 1u;
    snapshot->state = CAMERA_CAPTURE_READY;
}
