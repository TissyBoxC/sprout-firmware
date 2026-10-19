#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "voice_wake.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t observation_sequence;
    bool initialized;
    bool has_pending;
    voice_wake_event_t event;
} voice_session_wake_observer_t;

typedef enum {
    VOICE_SESSION_WAKE_OBSERVATION_NONE = 0,
    VOICE_SESSION_WAKE_OBSERVATION_NEW,
    VOICE_SESSION_WAKE_OBSERVATION_INVALID,
} voice_session_wake_observation_result_t;

typedef struct {
    voice_session_wake_observation_result_t result;
    uint32_t new_detections;
    voice_wake_event_t event;
} voice_session_wake_observation_t;

void voice_session_wake_observer_reset(
    voice_session_wake_observer_t *observer,
    uint32_t baseline_sequence
);

voice_session_wake_observation_t voice_session_wake_observer_observe(
    voice_session_wake_observer_t *observer,
    const voice_wake_snapshot_t *snapshot
);

bool voice_session_wake_observer_peek(
    const voice_session_wake_observer_t *observer,
    voice_wake_event_t *event_out
);

/**
 * @brief Take the pending observation and clear it in the observer.
 *
 * Returns true exactly once for an observed event. A baseline or invalid
 * snapshot returns false, so a later poll cannot replay an old wake.
 */
bool voice_session_wake_observer_take(
    voice_session_wake_observer_t *observer,
    voice_wake_event_t *event_out
);

void voice_session_wake_observer_clear_pending(
    voice_session_wake_observer_t *observer
);

uint32_t voice_session_wake_observer_advance(uint32_t value);

#ifdef __cplusplus
}
#endif
