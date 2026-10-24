#pragma once

// Sensor access for the camera capture lifecycle.
//
// This header is private to the component: the lifecycle owns whether a capture
// is allowed, and only it may ask the sensor for pixels. Nothing here is
// reachable from another component, so consent cannot be bypassed by calling
// the driver directly.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// The driver is only compiled in when the feature, the capability, and a sensor
// model are all selected. Every caller must treat a missing driver as
// "camera unavailable" rather than as an error to retry.
#if CONFIG_FEATURE_CAMERA_CAPTURE && CONFIG_CAMERA_SENSOR_OV2640 && \
    __has_include("esp_camera.h")
#define CAMERA_CAPTURE_HAS_SENSOR 1
#else
#define CAMERA_CAPTURE_HAS_SENSOR 0
#endif

/** @brief One still read out of the sensor. */
typedef struct {
    /** @brief True only when the buffer holds a complete JPEG frame. */
    bool ok;
    uint32_t width;
    uint32_t height;
    size_t length;
} camera_capture_sensor_frame_t;

/**
 * @brief Start the sensor. Safe to call more than once.
 *
 * @return ESP_OK when the sensor is running, otherwise the driver error. A
 *         board whose pins are wrong fails here rather than in a capture.
 */
esp_err_t camera_capture_sensor_init(void);

/** @brief Return true after a successful sensor start. */
bool camera_capture_sensor_is_ready(void);

/**
 * @brief Copy one whole JPEG still into caller memory.
 *
 * The result carries ok = false for an unusable read, a non-JPEG frame, or a
 * still larger than the buffer; the driver buffer is always returned.
 */
camera_capture_sensor_frame_t camera_capture_sensor_read(
    uint8_t *buffer,
    size_t buffer_size
);

/** @brief Stop the sensor and release the driver. */
void camera_capture_sensor_deinit(void);

#ifdef __cplusplus
}
#endif
