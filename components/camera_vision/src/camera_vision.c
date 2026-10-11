#include "camera_vision.h"

#include <string.h>

#include "esp_log.h"

#include "camera_vision_core.h"
#include "privacy_guard.h"

static const char *const CAMERA_VISION_TAG = "sprout_vision";

static bool camera_vision_ready;
static camera_vision_snapshot_t camera_vision_snapshot = {
    .task_id = {0},
    .task_type = CAMERA_VISION_TASK_OBJECT_RECOGNITION,
    .state = CAMERA_VISION_STATE_IDLE,
    .sequence = 0,
    .failure_count = 0,
};

static bool camera_vision_image_consent(void) {
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
}

esp_err_t camera_vision_init(void) {
    if (camera_vision_ready) {
        return ESP_OK;
    }
    camera_vision_snapshot.state = CAMERA_VISION_STATE_READY;
    camera_vision_ready = true;
    ESP_LOGI(
        CAMERA_VISION_TAG,
        "vision pipeline ready, image upload consent=%s",
        camera_vision_image_consent() ? "granted" : "denied"
    );
    return ESP_OK;
}

bool camera_vision_is_ready(void) {
    return camera_vision_ready;
}

camera_vision_snapshot_t camera_vision_get_snapshot(void) {
    return camera_vision_snapshot;
}

camera_vision_error_t camera_vision_accept_task(
    const char *task_id,
    camera_vision_task_type_t task_type
) {
    if (!camera_vision_ready) {
        return CAMERA_VISION_ERR_NOT_READY;
    }
    if (camera_vision_snapshot.state == CAMERA_VISION_STATE_ACKNOWLEDGED) {
        return CAMERA_VISION_ERR_BUSY;
    }
    if (!camera_vision_core_task_id_is_valid(task_id)) {
        return CAMERA_VISION_ERR_NULL_ARGUMENT;
    }
    if (task_type < 0 || task_type >= CAMERA_VISION_TASK_COUNT) {
        return CAMERA_VISION_ERR_UNSUPPORTED;
    }
    // Consent is re-checked on every task, so a revocation stops the next
    // capture instead of silently continuing.
    if (!camera_vision_image_consent()) {
        camera_vision_snapshot.state = CAMERA_VISION_STATE_ERROR;
        return CAMERA_VISION_ERR_PRIVACY_CONSENT_REQUIRED;
    }
    memset(camera_vision_snapshot.task_id, 0, sizeof(camera_vision_snapshot.task_id));
    const size_t length = strlen(task_id);
    if (length >= sizeof(camera_vision_snapshot.task_id)) {
        return CAMERA_VISION_ERR_NULL_ARGUMENT;
    }
    memcpy(camera_vision_snapshot.task_id, task_id, length);
    camera_vision_snapshot.task_type = task_type;
    camera_vision_snapshot.state = CAMERA_VISION_STATE_READY;
    camera_vision_snapshot.sequence += 1u;
    return CAMERA_VISION_OK;
}

camera_vision_error_t camera_vision_acknowledge(void) {
    if (!camera_vision_ready) {
        return CAMERA_VISION_ERR_NOT_READY;
    }
    if (camera_vision_snapshot.task_id[0] == '\0') {
        return CAMERA_VISION_ERR_BUSY;
    }
    if (!camera_vision_image_consent()) {
        camera_vision_snapshot.state = CAMERA_VISION_STATE_ERROR;
        return CAMERA_VISION_ERR_PRIVACY_CONSENT_REQUIRED;
    }
    camera_vision_snapshot.state = CAMERA_VISION_STATE_ACKNOWLEDGED;
    return CAMERA_VISION_OK;
}

int camera_vision_encode_result(
    const camera_vision_label_t *labels,
    size_t label_count,
    const char *summary,
    char *output,
    size_t output_size
) {
    return camera_vision_core_encode_result(
        camera_vision_snapshot.task_id,
        labels,
        label_count,
        summary,
        output,
        output_size
    );
}

camera_vision_error_t camera_vision_fail(const char *failure_code) {
    if (!camera_vision_ready) {
        return CAMERA_VISION_ERR_NOT_READY;
    }
    if (failure_code == NULL) {
        return CAMERA_VISION_ERR_NULL_ARGUMENT;
    }
    camera_vision_snapshot.failure_count += 1u;
    camera_vision_snapshot.state = CAMERA_VISION_STATE_READY;
    return CAMERA_VISION_OK;
}

camera_vision_error_t camera_vision_complete(void) {
    if (!camera_vision_ready) {
        return CAMERA_VISION_ERR_NOT_READY;
    }
    if (camera_vision_snapshot.state != CAMERA_VISION_STATE_ACKNOWLEDGED) {
        return CAMERA_VISION_ERR_BUSY;
    }
    camera_vision_snapshot.state = CAMERA_VISION_STATE_READY;
    camera_vision_snapshot.task_id[0] = '\0';
    return CAMERA_VISION_OK;
}

const char *camera_vision_task_type_name(camera_vision_task_type_t task_type) {
    return camera_vision_core_task_type_name(task_type);
}

bool camera_vision_task_type_from_name(
    const char *name,
    camera_vision_task_type_t *task_type_out
) {
    if (name == NULL || task_type_out == NULL) {
        return false;
    }
    for (int index = CAMERA_VISION_TASK_OBJECT_RECOGNITION;
         index < CAMERA_VISION_TASK_COUNT;
         ++index) {
        const char *candidate =
            camera_vision_core_task_type_name(
                (camera_vision_task_type_t)index
            );
        if (candidate != NULL && strcmp(candidate, name) == 0) {
            *task_type_out = (camera_vision_task_type_t)index;
            return true;
        }
    }
    return false;
}

const char *camera_vision_error_name(camera_vision_error_t error) {
    switch (error) {
        case CAMERA_VISION_OK:
            return "ok";
        case CAMERA_VISION_ERR_NULL_ARGUMENT:
            return "null_argument";
        case CAMERA_VISION_ERR_NOT_READY:
            return "not_ready";
        case CAMERA_VISION_ERR_BUSY:
            return "busy";
        case CAMERA_VISION_ERR_UNSUPPORTED:
            return "unsupported";
        case CAMERA_VISION_ERR_PRIVACY_CONSENT_REQUIRED:
            return "consent_required";
        case CAMERA_VISION_ERR_INVALID_LABEL:
            return "invalid_label";
        case CAMERA_VISION_ERR_TOO_MANY_LABELS:
            return "too_many_labels";
        default:
            return "unknown";
    }
}

const module_descriptor_t *camera_vision_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "camera_vision",
        .version = "1.0.0",
        .initialize = camera_vision_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
