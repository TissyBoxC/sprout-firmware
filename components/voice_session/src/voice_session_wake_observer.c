#include "voice_session_wake_observer.h"

#include <string.h>

uint32_t voice_session_wake_observer_advance(uint32_t value) {
    return value == UINT32_MAX ? 1u : value + 1u;
}

void voice_session_wake_observer_reset(
    voice_session_wake_observer_t *observer,
    uint32_t baseline_sequence
) {
    if (observer == NULL) {
        return;
    }
    memset(observer, 0, sizeof(*observer));
    observer->observation_sequence = baseline_sequence;
    observer->initialized = true;
}

voice_session_wake_observation_t voice_session_wake_observer_observe(
    voice_session_wake_observer_t *observer,
    const voice_wake_snapshot_t *snapshot
) {
    voice_session_wake_observation_t observation = {};
    observation.result = VOICE_SESSION_WAKE_OBSERVATION_NONE;
    if (observer == NULL || snapshot == NULL) {
        return observation;
    }
    if (!observer->initialized) {
        voice_session_wake_observer_reset(
            observer,
            snapshot->observation_sequence
        );
        const voice_wake_event_t *latest = &snapshot->latest_event;
        if (snapshot->observation_sequence != 0u &&
            latest->wake_word_id != 0u &&
            latest->wake_word_name[0] != '\0' &&
            latest->confidence_milli > 0u &&
            latest->confidence_milli <= 1000u) {
            observer->event = *latest;
            observer->has_pending = true;
            observation.result = VOICE_SESSION_WAKE_OBSERVATION_NEW;
            observation.new_detections = 1u;
            observation.event = *latest;
        }
        return observation;
    }
    if (snapshot->observation_sequence == observer->observation_sequence) {
        return observation;
    }

    uint32_t new_detections = 1u;
    if (snapshot->observation_sequence == 0u) {
        // A reset detector must not reuse an old pending event or count the
        // reset as a fresh wake.
        observer->observation_sequence = 0u;
        observer->has_pending = false;
        memset(&observer->event, 0, sizeof(observer->event));
        return observation;
    }
    if (observer->observation_sequence == 0u) {
        new_detections = 1u;
    } else if (snapshot->observation_sequence > observer->observation_sequence) {
        new_detections =
            snapshot->observation_sequence - observer->observation_sequence;
    } else {
        // The producer wraps from UINT32_MAX to one. Only the two direct wrap
        // edges are one event. Other lower values are a reset/reinit and must
        // establish a fresh baseline, not replay a gigantic backlog.
        if (!(observer->observation_sequence == UINT32_MAX &&
              snapshot->observation_sequence == 1u)) {
            observer->observation_sequence =
                snapshot->observation_sequence;
            observer->has_pending = false;
            return observation;
        }
        new_detections = 1u;
    }
    observer->observation_sequence = snapshot->observation_sequence;

    const voice_wake_event_t *latest = &snapshot->latest_event;
    if (latest->wake_word_id == 0u || latest->wake_word_name[0] == '\0' ||
        latest->confidence_milli > 1000u) {
        observer->has_pending = false;
        memset(&observer->event, 0, sizeof(observer->event));
        observation.result = VOICE_SESSION_WAKE_OBSERVATION_INVALID;
        return observation;
    }
    if (latest->confidence_milli == 0u) {
        observer->has_pending = false;
        memset(&observer->event, 0, sizeof(observer->event));
        observation.result = VOICE_SESSION_WAKE_OBSERVATION_INVALID;
        return observation;
    }
    observer->event = *latest;
    observer->has_pending = true;
    observation.result = VOICE_SESSION_WAKE_OBSERVATION_NEW;
    observation.new_detections = new_detections;
    observation.event = *latest;
    return observation;
}

bool voice_session_wake_observer_peek(
    const voice_session_wake_observer_t *observer,
    voice_wake_event_t *event_out
) {
    if (observer == NULL || event_out == NULL || !observer->has_pending) {
        return false;
    }
    *event_out = observer->event;
    return true;
}

bool voice_session_wake_observer_take(
    voice_session_wake_observer_t *observer,
    voice_wake_event_t *event_out
) {
    if (!voice_session_wake_observer_peek(observer, event_out)) {
        return false;
    }
    voice_session_wake_observer_clear_pending(observer);
    return true;
}

void voice_session_wake_observer_clear_pending(
    voice_session_wake_observer_t *observer
) {
    if (observer == NULL) {
        return;
    }
    observer->has_pending = false;
    memset(&observer->event, 0, sizeof(observer->event));
}
