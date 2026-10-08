#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Longest stable decision reason copied into diagnostics. */
#define PARENT_CONTROL_REASON_SIZE 48

/**
 * @brief Stable policy decisions shared by every consumption entry point.
 *
 * Values are stable so device UI, diagnostics, and the platform can map a
 * denial without parsing log text.
 */
typedef enum {
    PARENT_CONTROL_DECISION_ALLOWED = 0,
    PARENT_CONTROL_REASON_DISABLED_PERIOD,
    PARENT_CONTROL_REASON_CATEGORY_DENIED,
    PARENT_CONTROL_REASON_DAILY_LIMIT_REACHED,
    PARENT_CONTROL_REASON_TIME_UNTRUSTED,
    PARENT_CONTROL_REASON_POLICY_UNAVAILABLE,
} parent_control_reason_t;

/** @brief One evaluated decision with the reason that produced it. */
typedef struct {
    parent_control_reason_t reason;
    char reason_code[PARENT_CONTROL_REASON_SIZE];
    int64_t policy_version;
    bool policy_available;
} parent_control_decision_t;

/** @brief Policy-evaluation inputs for one consumption attempt. */
typedef struct {
    const char *category;
    bool safety_exempt;
    int64_t utc_epoch_seconds;
    int32_t timezone_offset_minutes;
} parent_control_request_t;

/** @brief Initialize the evaluator; requires policy, ledger, and time modules. */
esp_err_t parent_control_runtime_init(void);

/** @brief Return true when the evaluator is ready. */
bool parent_control_runtime_is_ready(void);

/**
 * @brief Evaluate one consumption attempt against the effective policy.
 *
 * This is the only gate used by wake detection, voice sessions, content
 * download, content playback, and the playback queue. Denials are logged to
 * the usage ledger as blocked counters before returning the decision.
 */
esp_err_t parent_control_evaluate(
    const parent_control_request_t *request,
    parent_control_decision_t *decision_out
);

/**
 * @brief Evaluate whether the currently active activity may continue.
 *
 * Returns the same stable reasons as parent_control_evaluate. Called on a
 * policy refresh and periodic tick so an active session stops promptly when
 * it becomes disallowed.
 */
esp_err_t parent_control_evaluate_active(
    const char *category,
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    parent_control_decision_t *decision_out
);

/**
 * @brief Evaluate without recording blocked counters.
 *
 * Used by continuation checks and safety-exempt probes so a periodic caller
 * cannot inflate the guardian-visible blocked totals. Safety-class audio passes
 * safety_exempt=true to test exemption without a real attempt.
 */
esp_err_t parent_control_evaluate_norecord(
    const char *category,
    bool safety_exempt,
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    parent_control_decision_t *decision_out
);

/**
 * @brief Commit one completed conversation and its elapsed seconds.
 *
 * Voice sessions call this once when a session ends so the ledger records the
 * conversation count and duration for the local day the session completed on.
 */
esp_err_t parent_control_note_conversation_finished(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    uint32_t conversation_seconds
);

/** @brief Commit one completed content playback to the usage ledger. */
esp_err_t parent_control_note_content_finished(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    const char *category,
    uint32_t content_seconds
);

/** @brief Return the stable text for one decision reason. */
const char *parent_control_reason_name(parent_control_reason_t reason);

/** @brief Return true when the decision denies an activity. */
bool parent_control_decision_is_denied(
    const parent_control_decision_t *decision
);

/** @brief Return true when safety-class audio bypasses normal blocks. */
bool parent_control_request_is_safety_exempt(
    const parent_control_request_t *request
);

/** @brief Release runtime resources. */
void parent_control_runtime_shutdown(void);

/** @brief Return the removable-module descriptor for parent_control_runtime. */
const module_descriptor_t *parent_control_runtime_module_descriptor(void);

#ifdef __cplusplus
}
#endif
