#include "diagnostic_reporter_state.h"

#include <string.h>

#define DIAGNOSTIC_REPORTER_INTERACTION_DURATION_MAX_MS 3600000u
#define DIAGNOSTIC_REPORTER_TOTAL_EVENT_CAPACITY \
    (DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY + \
     DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY + \
     DIAGNOSTIC_REPORTER_INTERACTION_EVENT_CAPACITY + 1u)

typedef struct {
    uint32_t values[DIAGNOSTIC_REPORTER_TOTAL_EVENT_CAPACITY];
    size_t count;
    uint32_t global_maximum;
} diagnostic_reporter_sequence_set_t;

static bool diagnostic_reporter_boot_event_is_valid(
    const diagnostic_reporter_boot_event_t *event
) {
    return event->sequence != 0 && event->boot_count != 0 &&
        diagnostic_reporter_text_is_terminated(
            event->event_id,
            sizeof(event->event_id)
        ) &&
        diagnostic_reporter_text_is_terminated(
            event->reset_reason,
            sizeof(event->reset_reason)
        ) &&
        diagnostic_reporter_text_is_terminated(
            event->firmware_version,
            sizeof(event->firmware_version)
        );
}

static bool diagnostic_reporter_failure_is_valid(
    const diagnostic_reporter_failure_state_t *failure
) {
    if (failure->pending > 1) {
        return false;
    }
    if (!failure->pending) {
        return true;
    }
    return failure->sequence != 0 && failure->failure_count != 0 &&
        diagnostic_reporter_text_is_terminated(
            failure->module_name,
            sizeof(failure->module_name)
        ) &&
        diagnostic_reporter_text_is_terminated(
            failure->error_code,
            sizeof(failure->error_code)
        ) &&
        diagnostic_reporter_text_is_terminated(
            failure->firmware_version,
            sizeof(failure->firmware_version)
        );
}

static bool diagnostic_reporter_recovery_event_is_valid(
    const diagnostic_reporter_recovery_state_t *event
) {
    return event->sequence != 0 &&
        diagnostic_reporter_text_is_terminated(
            event->event_id,
            sizeof(event->event_id)
        ) &&
        diagnostic_reporter_text_is_terminated(
            event->module_name,
            sizeof(event->module_name)
        ) &&
        diagnostic_reporter_text_is_terminated(
            event->firmware_version,
            sizeof(event->firmware_version)
        );
}

static bool diagnostic_reporter_interaction_type_is_valid(
    const char *event_type
) {
    if (event_type == NULL) {
        return false;
    }
    return strcmp(event_type, "wake_detected") == 0 ||
        strcmp(event_type, "wake_rejected") == 0 ||
        strcmp(event_type, "button_gesture") == 0 ||
        strcmp(event_type, "indicator_state") == 0 ||
        strcmp(event_type, "factory_reset_requested") == 0 ||
        strcmp(event_type, "factory_reset_cancelled") == 0 ||
        strcmp(event_type, "factory_reset_completed") == 0 ||
        strcmp(event_type, "factory_reset_failed") == 0;
}

static bool diagnostic_reporter_detail_code_is_valid(
    const char *detail_code
) {
    if (!diagnostic_reporter_text_is_terminated(
            detail_code,
            DIAGNOSTIC_REPORTER_DETAIL_CODE_SIZE
        )) {
        return false;
    }
    const size_t length = strlen(detail_code);
    if (length < 1 || length > DIAGNOSTIC_REPORTER_DETAIL_CODE_SIZE - 1) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const char value = detail_code[index];
        const bool is_ascii_letter =
            (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z');
        const bool is_ascii_digit = value >= '0' && value <= '9';
        const bool is_symbol = value == '_' || value == '.' ||
            value == ':' || value == '-';
        if (!is_ascii_letter && !is_ascii_digit && !is_symbol) {
            return false;
        }
    }
    return true;
}

static bool diagnostic_reporter_interaction_event_is_valid(
    const diagnostic_reporter_interaction_state_t *event
) {
    return event->sequence != 0 &&
        event->duration_ms <= DIAGNOSTIC_REPORTER_INTERACTION_DURATION_MAX_MS &&
        diagnostic_reporter_text_is_terminated(
            event->event_id,
            sizeof(event->event_id)
        ) &&
        diagnostic_reporter_text_is_terminated(
            event->event_type,
            sizeof(event->event_type)
        ) &&
        diagnostic_reporter_text_is_terminated(
            event->detail_code,
            sizeof(event->detail_code)
        ) &&
        diagnostic_reporter_text_is_terminated(
            event->firmware_version,
            sizeof(event->firmware_version)
        ) &&
        diagnostic_reporter_interaction_type_is_valid(event->event_type) &&
        diagnostic_reporter_detail_code_is_valid(event->detail_code);
}

static bool diagnostic_reporter_sequence_set_add(
    diagnostic_reporter_sequence_set_t *set,
    uint32_t sequence,
    uint32_t previous_in_category
) {
    if (sequence == 0 || sequence <= previous_in_category) {
        return false;
    }
    for (size_t index = 0; index < set->count; ++index) {
        if (set->values[index] == sequence) {
            return false;
        }
    }
    if (set->count >= DIAGNOSTIC_REPORTER_TOTAL_EVENT_CAPACITY) {
        return false;
    }
    set->values[set->count++] = sequence;
    if (sequence > set->global_maximum) {
        set->global_maximum = sequence;
    }
    return true;
}

static bool diagnostic_reporter_next_sequence_is_valid(
    uint32_t next_sequence,
    const diagnostic_reporter_sequence_set_t *set
) {
    if (next_sequence == 0) {
        return false;
    }
    return set->global_maximum == 0 || next_sequence > set->global_maximum;
}

static bool diagnostic_reporter_boot_events_are_valid(
    size_t count,
    const diagnostic_reporter_boot_event_t *events,
    diagnostic_reporter_sequence_set_t *set
) {
    uint32_t previous = 0;
    for (size_t index = 0; index < count; ++index) {
        const diagnostic_reporter_boot_event_t *event = &events[index];
        if (!diagnostic_reporter_boot_event_is_valid(event) ||
            !diagnostic_reporter_sequence_set_add(
                set,
                event->sequence,
                previous
            )) {
            return false;
        }
        previous = event->sequence;
    }
    return true;
}

static bool diagnostic_reporter_failure_sequence_is_valid(
    const diagnostic_reporter_failure_state_t *failure,
    diagnostic_reporter_sequence_set_t *set
) {
    if (!failure->pending) {
        return true;
    }
    return diagnostic_reporter_sequence_set_add(
        set,
        failure->sequence,
        0
    );
}

static bool diagnostic_reporter_recovery_events_are_valid(
    size_t count,
    const diagnostic_reporter_recovery_state_t *events,
    diagnostic_reporter_sequence_set_t *set
) {
    uint32_t previous = 0;
    for (size_t index = 0; index < count; ++index) {
        const diagnostic_reporter_recovery_state_t *event = &events[index];
        if (!diagnostic_reporter_recovery_event_is_valid(event) ||
            !diagnostic_reporter_sequence_set_add(
                set,
                event->sequence,
                previous
            )) {
            return false;
        }
        previous = event->sequence;
    }
    return true;
}

static bool diagnostic_reporter_interaction_events_are_valid(
    size_t count,
    const diagnostic_reporter_interaction_state_t *events,
    diagnostic_reporter_sequence_set_t *set
) {
    uint32_t previous = 0;
    for (size_t index = 0; index < count; ++index) {
        const diagnostic_reporter_interaction_state_t *event = &events[index];
        if (!diagnostic_reporter_interaction_event_is_valid(event) ||
            !diagnostic_reporter_sequence_set_add(
                set,
                event->sequence,
                previous
            )) {
            return false;
        }
        previous = event->sequence;
    }
    return true;
}

bool diagnostic_reporter_text_is_terminated(
    const char *value,
    size_t buffer_size
) {
    if (value == NULL || buffer_size == 0) {
        return false;
    }
    for (size_t index = 0; index < buffer_size; ++index) {
        if (value[index] == '\0') {
            return true;
        }
    }
    return false;
}

bool diagnostic_reporter_v3_is_valid(
    const diagnostic_reporter_state_v3_t *state
) {
    if (state == NULL ||
        state->magic != DIAGNOSTIC_REPORTER_STATE_MAGIC ||
        state->version != DIAGNOSTIC_REPORTER_STATE_VERSION_V3 ||
        state->boot_event_count > DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY ||
        state->recovery_event_count >
            DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY ||
        state->source_boot_count > state->boot_count ||
        !diagnostic_reporter_failure_is_valid(&state->failure)) {
        return false;
    }

    diagnostic_reporter_sequence_set_t set = {};
    return diagnostic_reporter_boot_events_are_valid(
            state->boot_event_count,
            state->boot_events,
            &set
        ) &&
        diagnostic_reporter_failure_sequence_is_valid(
            &state->failure,
            &set
        ) &&
        diagnostic_reporter_recovery_events_are_valid(
            state->recovery_event_count,
            state->recovery_events,
            &set
        ) &&
        diagnostic_reporter_next_sequence_is_valid(
            state->next_sequence,
            &set
        );
}

bool diagnostic_reporter_v4_is_valid(
    const diagnostic_reporter_state_v4_t *state
) {
    if (state == NULL ||
        state->magic != DIAGNOSTIC_REPORTER_STATE_MAGIC ||
        state->version != DIAGNOSTIC_REPORTER_STATE_VERSION_V4 ||
        state->boot_event_count > DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY ||
        state->recovery_event_count >
            DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY ||
        state->interaction_event_count >
            DIAGNOSTIC_REPORTER_INTERACTION_EVENT_CAPACITY ||
        state->source_boot_count > state->boot_count ||
        !diagnostic_reporter_failure_is_valid(&state->failure)) {
        return false;
    }

    diagnostic_reporter_sequence_set_t set = {};
    return diagnostic_reporter_boot_events_are_valid(
            state->boot_event_count,
            state->boot_events,
            &set
        ) &&
        diagnostic_reporter_failure_sequence_is_valid(
            &state->failure,
            &set
        ) &&
        diagnostic_reporter_recovery_events_are_valid(
            state->recovery_event_count,
            state->recovery_events,
            &set
        ) &&
        diagnostic_reporter_interaction_events_are_valid(
            state->interaction_event_count,
            state->interaction_events,
            &set
        ) &&
        diagnostic_reporter_next_sequence_is_valid(
            state->next_sequence,
            &set
        );
}

bool diagnostic_reporter_v5_is_valid(
    const diagnostic_reporter_state_t *state
) {
    if (state == NULL ||
        state->magic != DIAGNOSTIC_REPORTER_STATE_MAGIC ||
        state->version != DIAGNOSTIC_REPORTER_STATE_VERSION ||
        state->boot_event_count > DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY ||
        state->recovery_event_count >
            DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY ||
        state->interaction_event_count >
            DIAGNOSTIC_REPORTER_INTERACTION_EVENT_CAPACITY ||
        state->source_boot_count > state->boot_count ||
        !diagnostic_reporter_failure_is_valid(&state->failure)) {
        return false;
    }

    diagnostic_reporter_sequence_set_t set = {};
    return diagnostic_reporter_boot_events_are_valid(
            state->boot_event_count,
            state->boot_events,
            &set
        ) &&
        diagnostic_reporter_failure_sequence_is_valid(
            &state->failure,
            &set
        ) &&
        diagnostic_reporter_recovery_events_are_valid(
            state->recovery_event_count,
            state->recovery_events,
            &set
        ) &&
        diagnostic_reporter_interaction_events_are_valid(
            state->interaction_event_count,
            state->interaction_events,
            &set
        ) &&
        diagnostic_reporter_next_sequence_is_valid(
            state->next_sequence,
            &set
        );
}

bool diagnostic_reporter_acknowledge_state(
    diagnostic_reporter_state_t *state,
    uint32_t through_sequence
) {
    if (state == NULL || through_sequence == 0) {
        return state != NULL;
    }

    size_t retained_count = 0;
    for (size_t index = 0; index < state->boot_event_count; ++index) {
        const diagnostic_reporter_boot_event_t *event =
            &state->boot_events[index];
        if (event->sequence > through_sequence) {
            state->boot_events[retained_count++] = *event;
        }
    }
    state->boot_event_count = retained_count;

    size_t retained_recovery_count = 0;
    for (size_t index = 0; index < state->recovery_event_count; ++index) {
        const diagnostic_reporter_recovery_state_t *event =
            &state->recovery_events[index];
        if (event->sequence > through_sequence) {
            state->recovery_events[retained_recovery_count++] = *event;
        }
    }
    state->recovery_event_count = retained_recovery_count;

    size_t retained_interaction_count = 0;
    for (size_t index = 0; index < state->interaction_event_count; ++index) {
        const diagnostic_reporter_interaction_state_t *event =
            &state->interaction_events[index];
        if (event->sequence > through_sequence) {
            state->interaction_events[retained_interaction_count++] = *event;
        }
    }
    state->interaction_event_count = retained_interaction_count;

    if (state->failure.pending &&
        state->failure.sequence <= through_sequence) {
        // Keep the delivered identity so the same source transition is not
        // emitted again on the next heartbeat.
        state->failure.pending = 0;
    }
    return true;
}

void diagnostic_reporter_migrate_v3(
    diagnostic_reporter_state_t *destination,
    const diagnostic_reporter_state_v3_t *source
) {
    if (destination == NULL || source == NULL) {
        return;
    }
    memset(destination, 0, sizeof(*destination));
    destination->magic = DIAGNOSTIC_REPORTER_STATE_MAGIC;
    destination->version = DIAGNOSTIC_REPORTER_STATE_VERSION;
    destination->boot_event_count = source->boot_event_count;
    destination->next_sequence = source->next_sequence;
    destination->boot_count = source->boot_count;
    destination->dropped_boot_events = source->dropped_boot_events;
    destination->source_boot_count = source->source_boot_count;
    destination->last_failure_transition_count =
        source->last_failure_transition_count;
    destination->last_recovery_transition_count =
        source->last_recovery_transition_count;
    destination->failure = source->failure;
    destination->recovery_event_count = source->recovery_event_count;
    memcpy(
        destination->recovery_events,
        source->recovery_events,
        sizeof(destination->recovery_events)
    );
    memcpy(
        destination->boot_events,
        source->boot_events,
        sizeof(destination->boot_events)
    );
}

void diagnostic_reporter_migrate_v4(
    diagnostic_reporter_state_t *destination,
    const diagnostic_reporter_state_v4_t *source
) {
    if (destination == NULL || source == NULL) {
        return;
    }
    memset(destination, 0, sizeof(*destination));
    destination->magic = DIAGNOSTIC_REPORTER_STATE_MAGIC;
    destination->version = DIAGNOSTIC_REPORTER_STATE_VERSION;
    destination->boot_event_count = source->boot_event_count;
    destination->next_sequence = source->next_sequence;
    destination->boot_count = source->boot_count;
    destination->dropped_boot_events = source->dropped_boot_events;
    destination->source_boot_count = source->source_boot_count;
    destination->last_failure_transition_count =
        source->last_failure_transition_count;
    destination->last_recovery_transition_count =
        source->last_recovery_transition_count;
    destination->interaction_event_count = source->interaction_event_count;
    destination->failure = source->failure;
    destination->recovery_event_count = source->recovery_event_count;
    memcpy(
        destination->recovery_events,
        source->recovery_events,
        sizeof(destination->recovery_events)
    );
    memcpy(
        destination->interaction_events,
        source->interaction_events,
        sizeof(destination->interaction_events)
    );
    memcpy(
        destination->boot_events,
        source->boot_events,
        sizeof(destination->boot_events)
    );
}
