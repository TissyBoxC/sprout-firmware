#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "companionship.h"
#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Coarse device-side confidence attached to every reported sample.
 *
 * The companionship emotion comes from a bucket the platform or the model
 * assigned, not from a classifier that produces a calibrated score, so there is
 * no honest per-sample number to send. A fixed, conservative value tells the
 * guardian's distribution that every sample is a weak signal without inventing
 * a precision the device does not have.
 */
#define EMOTION_REPORT_CONFIDENCE 60

/** @brief Stable upload results recorded for diagnostics and status. */
typedef enum {
    EMOTION_REPORT_UPLOAD_OK = 0,
    /** Not attempted yet, or nothing is queued. */
    EMOTION_REPORT_UPLOAD_IDLE,
    /** Network, clock, or session not usable; the sample stays queued. */
    EMOTION_REPORT_UPLOAD_DEFERRED,
    /** A later attempt will succeed; the sample stays queued. */
    EMOTION_REPORT_UPLOAD_RETRY,
    /** A later attempt cannot help; the sample is dropped. */
    EMOTION_REPORT_UPLOAD_REJECTED,
} emotion_report_upload_t;

/**
 * @brief Queue one emotion observation for upload.
 *
 * Returns immediately and never performs network I/O: the caller may be the
 * audio or conversation task, and an upload must not delay the reply a child is
 * waiting for. The sample is dropped without any request when the module is
 * disabled, the device has no platform registration, or the family switch was
 * observed off. Only the emotion label, the fixed confidence, the observation
 * instant, and (when known) the child id are kept; any audio, image, or
 * conversation text stays with the caller.
 *
 * @return ESP_OK when the sample was queued; ESP_ERR_INVALID_STATE when the
 * module is not ready; ESP_ERR_INVALID_ARG for an unknown emotion or an
 * unusable instant; ESP_ERR_NO_MEM when the queue rejected the sample.
 */
esp_err_t emotion_report_observe(
    companionship_emotion_t emotion,
    int64_t observed_at_epoch_seconds
);

/**
 * @brief Upload the oldest queued sample synchronously.
 *
 * Uses POST /api/v1/devices/{device_id}/emotion-observations with the device
 * session token. A 401/403 clears the local token so the next attempt performs
 * a fresh challenge. A 403 is the family's mood-sensing switch being off and is
 * remembered so later samples are not queued until the toggle is re-read; a 404
 * or 422 is a stable mismatch the device cannot fix and the sample is dropped;
 * a transport failure, a missing clock, or a 5xx keeps the sample for the next
 * cycle. Before the request the local interval and backoff limits are checked,
 * so a caller-driven flush costs nothing while the worker is waiting.
 */
esp_err_t emotion_report_flush(void);

/** @brief Return true when a sample is queued and the limits allow an attempt. */
bool emotion_report_is_due(void);

/** @brief Return the upload result of the most recent attempt. */
emotion_report_upload_t emotion_report_last_upload(void);

/** @brief Return true once the last attempt saw the family switch is off. */
bool emotion_report_is_disabled_by_family(void);

/** @brief Return the removable-module descriptor for emotion reporting. */
const module_descriptor_t *emotion_report_module_descriptor(void);

#ifdef __cplusplus
}
#endif
