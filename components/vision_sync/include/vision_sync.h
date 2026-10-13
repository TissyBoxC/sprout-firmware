#pragma once

#include <stddef.h>

#include "camera_vision.h"
#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One bounded inference result produced on the device.
 *
 * The provider returns only symbolic labels and a short summary. Image bytes
 * never cross this interface, so the sync worker has no path to upload pixels.
 */
typedef struct {
    camera_vision_label_t labels[CAMERA_VISION_MAX_LABELS];
    size_t label_count;
    char summary[201];
    int frame_width;
    int frame_height;
    int frame_bytes;
} vision_sync_result_t;

/**
 * @brief On-device inference provider.
 *
 * A camera-capable image supplies a provider that runs the recognizer and
 * fills one bounded result. `analyze` returns ESP_OK on success,
 * ESP_ERR_NOT_SUPPORTED when the task type cannot be handled, and any other
 * error to report a bounded failure upstream.
 */
typedef esp_err_t (*vision_sync_analyze_fn)(
    const char *task_id,
    camera_vision_task_type_t task_type,
    const char *prompt,
    void *context,
    vision_sync_result_t *result_out
);

typedef struct {
    vision_sync_analyze_fn analyze;
    void *context;
} vision_sync_provider_t;

/**
 * @brief Register the on-device inference provider.
 *
 * Passing NULL removes the provider. Without a provider the worker still
 * drains the platform queue but reports each task as `unsupported` rather than
 * inventing a recognition result.
 */
esp_err_t vision_sync_register_provider(const vision_sync_provider_t *provider);

/** @brief Fetch pending tasks, run the provider and report each outcome once. */
esp_err_t vision_sync_now(void);

/** @brief Return the removable-module descriptor for vision task sync. */
const module_descriptor_t *vision_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
