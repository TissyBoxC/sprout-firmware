#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "emotion_report.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum label length including the terminator; the platform's longest is "frustrated". */
#define EMOTION_REPORT_LABEL_SIZE 16

/** @brief Maximum child id length including the terminator (a platform UUID is 36). */
#define EMOTION_REPORT_CHILD_ID_SIZE 40

/** @brief Longest RFC 3339 instant this component emits including the terminator. */
#define EMOTION_REPORT_OBSERVED_AT_SIZE 32

/**
 * @brief Maximum request body size.
 *
 * A UUID child id, the longest label, and a 20-character instant occupy well
 * under 200 bytes; the bound exists so a malformed observation is refused
 * instead of overflowing the caller's buffer.
 */
#define EMOTION_REPORT_BODY_SIZE 256

/**
 * @brief One queued observation, reduced to the four fields the platform accepts.
 *
 * There is deliberately no field for text, media, or a reason: the only way to
 * keep conversation content off the wire is to never have a place to put it.
 */
typedef struct {
    char label[EMOTION_REPORT_LABEL_SIZE];
    int confidence;
    int64_t observed_at_epoch_seconds;
    /** Empty when the device is not bound to a child; the platform resolves it. */
    char child_id[EMOTION_REPORT_CHILD_ID_SIZE];
} emotion_report_observation_t;

/** @brief Pure helpers shared by the device runtime and the host tests. */
esp_err_t emotion_report_core_make_observation(
    companionship_emotion_t emotion,
    int64_t observed_at_epoch_seconds,
    const char *child_id,
    emotion_report_observation_t *observation_out
);

/**
 * @brief Format an instant as an RFC 3339 UTC timestamp.
 *
 * Returns false for a non-positive instant, which means the device clock was
 * never trusted; a sample stamped then would land in 1970 and the platform
 * would refuse it as too old.
 */
bool emotion_report_core_observed_at(
    int64_t epoch_seconds,
    char *observed_at_out,
    size_t observed_at_size
);

/** @brief Write the request body; a negative result means it did not fit. */
int emotion_report_core_build_body(
    const emotion_report_observation_t *observation,
    char *body_out,
    size_t body_size
);

/**
 * @brief Translate an HTTP status into the upload class.
 *
 * 0 means no response was received. A 401/403 is retryable only after a fresh
 * session; 404/422 are stable mismatches that would fail identically on every
 * retry and therefore must not keep a sample queued forever.
 */
emotion_report_upload_t emotion_report_core_classify_status(int status_code);

#ifdef __cplusplus
}
#endif
