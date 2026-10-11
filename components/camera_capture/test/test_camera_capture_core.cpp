#include <cassert>

#include "camera_capture_core.h"

static void test_frame_bounds() {
    assert(!camera_capture_core_frame_is_valid(0, 240, 1024));
    assert(!camera_capture_core_frame_is_valid(15, 240, 1024));
    assert(!camera_capture_core_frame_is_valid(240, 15, 1024));
    assert(!camera_capture_core_frame_is_valid(240, 240, 0));
    assert(!camera_capture_core_frame_is_valid(
        240,
        240,
        CAMERA_CAPTURE_MAX_FRAME_BYTES + 1
    ));
    assert(camera_capture_core_frame_is_valid(240, 240, 1024));
    assert(camera_capture_core_frame_is_valid(
        CAMERA_CAPTURE_MAX_DIMENSION,
        CAMERA_CAPTURE_MAX_DIMENSION,
        CAMERA_CAPTURE_MAX_FRAME_BYTES
    ));
}

static void test_lifecycle_gates() {
    assert(camera_capture_core_can_start(CAMERA_CAPTURE_READY));
    assert(!camera_capture_core_can_start(CAMERA_CAPTURE_IDLE));
    assert(!camera_capture_core_can_start(CAMERA_CAPTURE_CAPTURING));
    assert(camera_capture_core_can_finish(CAMERA_CAPTURE_CAPTURING));
    assert(!camera_capture_core_can_finish(CAMERA_CAPTURE_READY));
}

static void test_apply_frame_and_drop() {
    camera_capture_snapshot_t snapshot = {
        .width = 0,
        .height = 0,
        .byte_size = 0,
        .sequence = 0,
        .dropped_frames = 0,
        .guardian_consent = true,
        .state = CAMERA_CAPTURE_CAPTURING,
    };
    camera_capture_core_apply_frame(&snapshot, 320, 240, 2048);
    assert(snapshot.sequence == 1);
    assert(snapshot.width == 320);
    assert(snapshot.height == 240);
    assert(snapshot.byte_size == 2048);
    assert(snapshot.state == CAMERA_CAPTURE_READY);

    camera_capture_core_record_drop(&snapshot);
    assert(snapshot.dropped_frames == 1);
    assert(snapshot.state == CAMERA_CAPTURE_READY);

    camera_capture_core_apply_frame(nullptr, 1, 1, 1);
    camera_capture_core_record_drop(nullptr);
}

int main() {
    test_frame_bounds();
    test_lifecycle_gates();
    test_apply_frame_and_drop();
    return 0;
}
