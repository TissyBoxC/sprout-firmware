#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bounded capture lifecycle states. */
typedef enum {
    CAMERA_CAPTURE_IDLE = 0,
    CAMERA_CAPTURE_READY,
    CAMERA_CAPTURE_CAPTURING,
    CAMERA_CAPTURE_ERROR,
} camera_capture_state_t;

/** @brief Stable errors returned by the capture path. */
typedef enum {
    CAMERA_CAPTURE_OK = 0,
    CAMERA_CAPTURE_ERR_NULL_ARGUMENT,
    CAMERA_CAPTURE_ERR_NOT_READY,
    CAMERA_CAPTURE_ERR_BUSY,
    CAMERA_CAPTURE_ERR_UNSUPPORTED,
    CAMERA_CAPTURE_ERR_PRIVACY_CONSENT_REQUIRED,
    CAMERA_CAPTURE_ERR_STORAGE,
    CAMERA_CAPTURE_ERR_INVALID_FRAME,
} camera_capture_error_t;

/**
 * @brief Bounded capture metadata shared with the platform.
 *
 * Only symbolic, non-identifying fields are reported. Image bytes never leave
 * the device through this struct and are never persisted by the component.
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t byte_size;
    uint32_t sequence;
    uint32_t dropped_frames;
    bool guardian_consent;
    camera_capture_state_t state;
} camera_capture_snapshot_t;

/** @brief Hard bounds mirrored by the platform so responses stay bounded. */
#define CAMERA_CAPTURE_MIN_DIMENSION 16u
#define CAMERA_CAPTURE_MAX_DIMENSION 4096u
#define CAMERA_CAPTURE_MAX_FRAME_BYTES (8u * 1024u * 1024u)

/**
 * @brief Initialize the capture pipeline.
 *
 * Returns ESP_ERR_NOT_SUPPORTED when the firmware image was built without the
 * camera capability, so callers can hide the feature instead of failing late.
 */
esp_err_t camera_capture_init(void);

/** @brief Return true after a successful initialization. */
bool camera_capture_is_ready(void);

/** @brief Return the current bounded capture snapshot. */
camera_capture_snapshot_t camera_capture_get_snapshot(void);

/**
 * @brief Start one capture frame.
 *
 * Image upload consent is evaluated before the sensor is started. Without
 * guardian consent the call fails with
 * CAMERA_CAPTURE_ERR_PRIVACY_CONSENT_REQUIRED and no frame is produced.
 */
camera_capture_error_t camera_capture_start(void);

/**
 * @brief Finish the active capture and record its bounded metadata.
 *
 * Dimensions and byte size are validated against the contract bounds; an
 * out-of-range frame is rejected and counted as a dropped frame.
 */
camera_capture_error_t camera_capture_finish(
    uint32_t width,
    uint32_t height,
    uint32_t byte_size
);

/** @brief Abort an in-flight capture without recording a frame. */
camera_capture_error_t camera_capture_abort(void);

/**
 * @brief Copy one whole JPEG still of the current scene into caller memory.
 *
 * The caller owns the buffer and must pass its capacity. The call reads the
 * sensor once, converts the frame to JPEG, and copies it out before releasing
 * the sensor buffer, so no driver memory outlives the call and no caller holds a
 * pointer into the DMA buffer.
 *
 * Consent gates the sensor, not this function: capture_start already refused
 * without it, so a caller that reaches here has a live, consented capture.
 *
 * @return CAMERA_CAPTURE_ERR_PRIVACY_CONSENT_REQUIRED when no consented capture
 *         is active, CAMERA_CAPTURE_ERR_NOT_READY when the sensor is not
 *         initialized or the driver read failed,
 *         CAMERA_CAPTURE_ERR_NULL_ARGUMENT for an unusable output buffer, and
 *         CAMERA_CAPTURE_ERR_INVALID_FRAME when the still does not fit.
 */
camera_capture_error_t camera_capture_read_jpeg(
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t *width_out,
    uint32_t *height_out,
    size_t *length_out
);

/** @brief Return the stable text for one capture error. */
const char *camera_capture_error_name(camera_capture_error_t error);

/** @brief Return the stable text for one capture state. */
const char *camera_capture_state_name(camera_capture_state_t state);

/** @brief Return the removable-module descriptor for camera_capture. */
const module_descriptor_t *camera_capture_module_descriptor(void);

#ifdef __cplusplus
}
#endif
