#include "camera_capture.h"

#include "esp_log.h"

#include "camera_capture_core.h"
#include "camera_capture_sensor.h"
#if CONFIG_FEATURE_PRIVACY_GUARD
#include "privacy_guard.h"
#endif

static const char *const CAMERA_CAPTURE_TAG = "sprout_camera";

static bool camera_capture_ready;
static camera_capture_snapshot_t camera_capture_snapshot = {
    .width = 0,
    .height = 0,
    .byte_size = 0,
    .sequence = 0,
    .dropped_frames = 0,
    .guardian_consent = false,
    .state = CAMERA_CAPTURE_IDLE,
};

static bool camera_capture_image_consent(void) {
#if CONFIG_FEATURE_PRIVACY_GUARD
    if (!privacy_guard_is_ready()) {
        return false;
    }
    privacy_guard_decision_t decision = {
        .data_class = PRIVACY_GUARD_DATA_IMAGE_UPLOAD,
        .allowed = false,
        .guardian_consent = false,
        .policy_requires_consent = true,
        .reason = PRIVACY_GUARD_ERR_CONSENT_REQUIRED,
    };
    if (privacy_guard_evaluate(
            PRIVACY_GUARD_DATA_IMAGE_UPLOAD,
            &decision
        ) != PRIVACY_GUARD_OK) {
        return false;
    }
    return decision.allowed;
#else
    // Without the privacy guard there is no consent source, so image upload
    // is denied rather than silently allowing capture.
    return false;
#endif
}

esp_err_t camera_capture_init(void) {
    if (camera_capture_ready) {
        // Re-arm after an error (for example a revoked consent) so a guardian
        // who grants consent again can resume captures without a reboot.
        if (camera_capture_snapshot.state != CAMERA_CAPTURE_CAPTURING) {
            camera_capture_snapshot.guardian_consent =
                camera_capture_image_consent();
            camera_capture_snapshot.state = CAMERA_CAPTURE_READY;
        }
        return ESP_OK;
    }
    // The component is only compiled for camera-capable images; the CMake
    // guard keeps it out of firmware that advertises no camera capability.
    camera_capture_snapshot.guardian_consent = camera_capture_image_consent();
    camera_capture_snapshot.state = CAMERA_CAPTURE_READY;
    camera_capture_ready = true;
    ESP_LOGI(
        CAMERA_CAPTURE_TAG,
        "camera ready, image upload consent=%s",
        camera_capture_snapshot.guardian_consent ? "granted" : "denied"
    );
    return ESP_OK;
}

bool camera_capture_is_ready(void) {
    return camera_capture_ready;
}

camera_capture_snapshot_t camera_capture_get_snapshot(void) {
    camera_capture_snapshot.guardian_consent = camera_capture_image_consent();
    return camera_capture_snapshot;
}

camera_capture_error_t camera_capture_start(void) {
    if (!camera_capture_ready) {
        return CAMERA_CAPTURE_ERR_NOT_READY;
    }
    if (!camera_capture_core_can_start(camera_capture_snapshot.state)) {
        return CAMERA_CAPTURE_ERR_BUSY;
    }
    // Consent is re-checked on every start so a revocation takes effect before
    // the next frame is produced, not only at initialization.
    if (!camera_capture_image_consent()) {
        camera_capture_snapshot.guardian_consent = false;
        camera_capture_snapshot.state = CAMERA_CAPTURE_ERROR;
        return CAMERA_CAPTURE_ERR_PRIVACY_CONSENT_REQUIRED;
    }
    camera_capture_snapshot.guardian_consent = true;
#if CAMERA_CAPTURE_HAS_SENSOR
    // The sensor is started only here, after consent passed, so a device whose
    // guardian never allowed image upload never powers the camera at all.
    const esp_err_t sensor_result = camera_capture_sensor_init();
    if (sensor_result != ESP_OK) {
        camera_capture_snapshot.state = CAMERA_CAPTURE_ERROR;
        return CAMERA_CAPTURE_ERR_NOT_READY;
    }
#endif
    camera_capture_snapshot.state = CAMERA_CAPTURE_CAPTURING;
    return CAMERA_CAPTURE_OK;
}

camera_capture_error_t camera_capture_finish(
    uint32_t width,
    uint32_t height,
    uint32_t byte_size
) {
    if (!camera_capture_ready) {
        return CAMERA_CAPTURE_ERR_NOT_READY;
    }
    if (!camera_capture_core_can_finish(camera_capture_snapshot.state)) {
        return CAMERA_CAPTURE_ERR_BUSY;
    }
    if (!camera_capture_core_frame_is_valid(width, height, byte_size)) {
        camera_capture_core_record_drop(&camera_capture_snapshot);
        return CAMERA_CAPTURE_ERR_INVALID_FRAME;
    }
    camera_capture_core_apply_frame(
        &camera_capture_snapshot,
        width,
        height,
        byte_size
    );
    return CAMERA_CAPTURE_OK;
}

camera_capture_error_t camera_capture_abort(void) {
    if (!camera_capture_ready) {
        return CAMERA_CAPTURE_ERR_NOT_READY;
    }
    if (camera_capture_snapshot.state != CAMERA_CAPTURE_CAPTURING) {
        return CAMERA_CAPTURE_ERR_BUSY;
    }
    camera_capture_core_record_drop(&camera_capture_snapshot);
    return CAMERA_CAPTURE_OK;
}

camera_capture_error_t camera_capture_read_jpeg(
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t *width_out,
    uint32_t *height_out,
    size_t *length_out
) {
    if (buffer == NULL || buffer_size == 0 || width_out == NULL ||
        height_out == NULL || length_out == NULL) {
        return CAMERA_CAPTURE_ERR_NULL_ARGUMENT;
    }
    if (!camera_capture_ready) {
        return CAMERA_CAPTURE_ERR_NOT_READY;
    }
    // A read outside a consented capture would hand out pixels the guardian has
    // not allowed, so the capture state, not only the consent flag, gates this.
    if (camera_capture_snapshot.state != CAMERA_CAPTURE_CAPTURING) {
        return CAMERA_CAPTURE_ERR_PRIVACY_CONSENT_REQUIRED;
    }
#if CAMERA_CAPTURE_HAS_SENSOR
    const camera_capture_sensor_frame_t frame =
        camera_capture_sensor_read(buffer, buffer_size);
    if (!frame.ok) {
        camera_capture_core_record_drop(&camera_capture_snapshot);
        return CAMERA_CAPTURE_ERR_INVALID_FRAME;
    }
    *width_out = frame.width;
    *height_out = frame.height;
    *length_out = frame.length;
    return CAMERA_CAPTURE_OK;
#else
    // No sensor model is selected, so the image cannot produce a frame. The
    // caller reports "camera unavailable" and the guardian sees that the view
    // has no image rather than a blank picture.
    return CAMERA_CAPTURE_ERR_UNSUPPORTED;
#endif
}

const char *camera_capture_error_name(camera_capture_error_t error) {
    switch (error) {
        case CAMERA_CAPTURE_OK:
            return "ok";
        case CAMERA_CAPTURE_ERR_NULL_ARGUMENT:
            return "null_argument";
        case CAMERA_CAPTURE_ERR_NOT_READY:
            return "not_ready";
        case CAMERA_CAPTURE_ERR_BUSY:
            return "busy";
        case CAMERA_CAPTURE_ERR_UNSUPPORTED:
            return "unsupported";
        case CAMERA_CAPTURE_ERR_PRIVACY_CONSENT_REQUIRED:
            return "consent_required";
        case CAMERA_CAPTURE_ERR_STORAGE:
            return "storage";
        case CAMERA_CAPTURE_ERR_INVALID_FRAME:
            return "invalid_frame";
        default:
            return "unknown";
    }
}

const char *camera_capture_state_name(camera_capture_state_t state) {
    switch (state) {
        case CAMERA_CAPTURE_IDLE:
            return "idle";
        case CAMERA_CAPTURE_READY:
            return "ready";
        case CAMERA_CAPTURE_CAPTURING:
            return "capturing";
        case CAMERA_CAPTURE_ERROR:
            return "error";
        default:
            return "unknown";
    }
}

const module_descriptor_t *camera_capture_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "camera_capture",
        .version = "1.0.0",
        .initialize = camera_capture_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
