#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable errors returned by the wake feedback module. */
typedef enum {
    WAKE_FEEDBACK_OK = 0,
    WAKE_FEEDBACK_ERR_NOT_INITIALIZED,
    WAKE_FEEDBACK_ERR_NOT_READY,
} wake_feedback_error_t;

/** @brief Bounded counters for diagnostics and tests. */
typedef struct {
    uint32_t accepted_events;
    uint32_t rejected_events;
    uint32_t cue_failures;
    uint32_t led_failures;
    uint32_t telemetry_failures;
    uint64_t last_wake_at_ms;
} wake_feedback_snapshot_t;

/**
 * @brief Register wake feedback on the initialized voice_wake detector.
 *
 * The module owns one callback registration. It does not start or stop the
 * microphone; voice_wake keeps that lifecycle so feedback cannot enable a
 * capture path by itself.
 */
esp_err_t wake_feedback_init(void);

/** @brief Return true when the feedback callback is registered. */
bool wake_feedback_is_ready(void);

/** @brief Return the bounded feedback counters. */
wake_feedback_snapshot_t wake_feedback_get_snapshot(void);

/** @brief Return the stable string for one feedback error. */
const char *wake_feedback_error_name(wake_feedback_error_t error);

/** @brief Return the removable-module descriptor for wake_feedback. */
const module_descriptor_t *wake_feedback_module_descriptor(void);

#ifdef __cplusplus
}
#endif
