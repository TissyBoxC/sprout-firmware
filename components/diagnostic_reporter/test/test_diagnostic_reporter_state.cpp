#include "diagnostic_reporter_state.h"

#include <stdio.h>
#include <string.h>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void copy_text(char *destination, size_t destination_size, const char *source) {
    snprintf(destination, destination_size, "%s", source);
}

diagnostic_reporter_boot_event_t make_boot_event(uint32_t sequence) {
    diagnostic_reporter_boot_event_t event = {};
    event.sequence = sequence;
    event.boot_count = sequence;
    copy_text(event.event_id, sizeof(event.event_id), "boot_event");
    copy_text(event.reset_reason, sizeof(event.reset_reason), "power_on");
    copy_text(event.firmware_version, sizeof(event.firmware_version), "0.1.0");
    return event;
}

diagnostic_reporter_failure_state_t make_failure(uint32_t sequence) {
    diagnostic_reporter_failure_state_t failure = {};
    failure.sequence = sequence;
    failure.failure_count = 2;
    failure.pending = 1;
    copy_text(failure.module_name, sizeof(failure.module_name), "voice_wake");
    copy_text(failure.error_code, sizeof(failure.error_code), "ESP_ERR_TIMEOUT");
    copy_text(
        failure.firmware_version,
        sizeof(failure.firmware_version),
        "0.1.0"
    );
    return failure;
}

diagnostic_reporter_recovery_state_t make_recovery(uint32_t sequence) {
    diagnostic_reporter_recovery_state_t event = {};
    event.sequence = sequence;
    copy_text(event.event_id, sizeof(event.event_id), "recovery_event");
    copy_text(event.module_name, sizeof(event.module_name), "voice_wake");
    copy_text(event.firmware_version, sizeof(event.firmware_version), "0.1.0");
    return event;
}

diagnostic_reporter_interaction_state_t make_interaction(
    uint32_t sequence,
    const char *event_type,
    const char *detail_code
) {
    diagnostic_reporter_interaction_state_t event = {};
    event.sequence = sequence;
    copy_text(event.event_id, sizeof(event.event_id), "interaction_event");
    copy_text(event.event_type, sizeof(event.event_type), event_type);
    copy_text(event.detail_code, sizeof(event.detail_code), detail_code);
    copy_text(event.firmware_version, sizeof(event.firmware_version), "0.1.0");
    return event;
}

diagnostic_reporter_state_v3_t make_v3_state() {
    diagnostic_reporter_state_v3_t state = {};
    state.magic = DIAGNOSTIC_REPORTER_STATE_MAGIC;
    state.version = DIAGNOSTIC_REPORTER_STATE_VERSION_V3;
    state.next_sequence = 4;
    state.boot_count = 2;
    state.source_boot_count = 2;
    state.boot_event_count = 1;
    state.boot_events[0] = make_boot_event(1);
    state.failure = make_failure(2);
    state.recovery_event_count = 1;
    state.recovery_events[0] = make_recovery(3);
    state.last_failure_transition_count = 2;
    state.last_recovery_transition_count = 3;
    return state;
}

diagnostic_reporter_state_v4_t make_v4_state() {
    diagnostic_reporter_state_v4_t state = {};
    state.magic = DIAGNOSTIC_REPORTER_STATE_MAGIC;
    state.version = DIAGNOSTIC_REPORTER_STATE_VERSION_V4;
    state.next_sequence = 5;
    state.boot_count = 2;
    state.source_boot_count = 2;
    state.boot_event_count = 1;
    state.boot_events[0] = make_boot_event(1);
    state.failure = make_failure(2);
    state.recovery_event_count = 1;
    state.recovery_events[0] = make_recovery(3);
    state.interaction_event_count = 1;
    state.interaction_events[0] = make_interaction(
        4,
        "wake_detected",
        "wake_1_confidence_0700"
    );
    return state;
}

void test_v3_migration_preserves_history() {
    const diagnostic_reporter_state_v3_t source = make_v3_state();
    diagnostic_reporter_state_t destination = {};
    diagnostic_reporter_migrate_v3(&destination, &source);

    check(
        diagnostic_reporter_v3_is_valid(&source),
        "v3 fixture must be valid"
    );
    check(
        diagnostic_reporter_v5_is_valid(&destination),
        "migrated v3 fixture must be valid"
    );
    check(destination.boot_count == source.boot_count, "v3 boot count");
    check(
        destination.boot_event_count == source.boot_event_count,
        "v3 boot event count"
    );
    check(
        destination.failure.pending == 1 &&
            destination.failure.sequence == source.failure.sequence,
        "v3 failure history"
    );
    check(
        destination.recovery_event_count == source.recovery_event_count &&
            destination.recovery_events[0].sequence == 3,
        "v3 recovery history"
    );
    check(
        destination.interaction_event_count == 0,
        "v3 has no interaction history"
    );
}

void test_v4_migration_preserves_interaction() {
    const diagnostic_reporter_state_v4_t source = make_v4_state();
    diagnostic_reporter_state_t destination = {};
    diagnostic_reporter_migrate_v4(&destination, &source);

    check(
        diagnostic_reporter_v4_is_valid(&source),
        "v4 fixture must be valid"
    );
    check(
        diagnostic_reporter_v5_is_valid(&destination),
        "migrated v4 fixture must be valid"
    );
    check(
        destination.interaction_event_count == 1 &&
            destination.interaction_events[0].sequence == 4,
        "v4 interaction history"
    );
    check(
        strcmp(
            destination.interaction_events[0].detail_code,
            "wake_1_confidence_0700"
        ) == 0,
        "v4 detail code"
    );
}

void test_wake_interaction_uses_zero_duration_and_ascii_detail() {
    diagnostic_reporter_state_t state = {};
    state.magic = DIAGNOSTIC_REPORTER_STATE_MAGIC;
    state.version = DIAGNOSTIC_REPORTER_STATE_VERSION;
    state.next_sequence = 3;
    state.boot_count = 1;
    state.source_boot_count = 1;
    state.boot_event_count = 1;
    state.boot_events[0] = make_boot_event(1);
    state.interaction_event_count = 1;
    state.interaction_events[0] = make_interaction(
        2,
        "wake_detected",
        "wake_1_confidence_0700"
    );
    state.interaction_events[0].duration_ms = 0;

    check(
        diagnostic_reporter_v5_is_valid(&state),
        "wake_detected with duration zero must be valid"
    );

    state.interaction_events[0].duration_ms = 1;
    check(
        diagnostic_reporter_v5_is_valid(&state),
        "non-zero real duration must remain valid for timed interactions"
    );

    state.interaction_events[0].detail_code[0] = '\xE9';
    state.interaction_events[0].detail_code[1] = '\0';
    check(
        !diagnostic_reporter_v5_is_valid(&state),
        "non-ASCII detail code must be rejected"
    );
}

void test_capacity_boundaries_are_bounded() {
    diagnostic_reporter_state_t state = {};
    state.magic = DIAGNOSTIC_REPORTER_STATE_MAGIC;
    state.version = DIAGNOSTIC_REPORTER_STATE_VERSION;
    state.next_sequence = DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY + 1;
    state.boot_count = DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY;
    state.source_boot_count = state.boot_count;
    state.boot_event_count = DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY;
    for (size_t index = 0; index < state.boot_event_count; ++index) {
        state.boot_events[index] = make_boot_event((uint32_t)index + 1);
    }
    check(
        diagnostic_reporter_v5_is_valid(&state),
        "full boot capacity must be valid"
    );

    state.boot_event_count = DIAGNOSTIC_REPORTER_BOOT_EVENT_CAPACITY + 1;
    check(
        !diagnostic_reporter_v5_is_valid(&state),
        "over-capacity boot count must be rejected"
    );
}

void test_sequences_must_be_strict_and_unique() {
    diagnostic_reporter_state_t state = {};
    state.magic = DIAGNOSTIC_REPORTER_STATE_MAGIC;
    state.version = DIAGNOSTIC_REPORTER_STATE_VERSION;
    state.next_sequence = 4;
    state.boot_count = 1;
    state.source_boot_count = 1;
    state.boot_event_count = 1;
    state.boot_events[0] = make_boot_event(1);
    state.recovery_event_count = 1;
    state.recovery_events[0] = make_recovery(3);
    state.interaction_event_count = 1;
    state.interaction_events[0] = make_interaction(
        2,
        "indicator_state",
        "listening"
    );
    check(
        diagnostic_reporter_v5_is_valid(&state),
        "interleaved category sequences must be valid"
    );

    state.interaction_events[0].sequence = 3;
    check(
        !diagnostic_reporter_v5_is_valid(&state),
        "duplicate sequence across categories must be rejected"
    );

    state.interaction_events[0].sequence = 2;
    state.recovery_events[0].sequence = 1;
    check(
        !diagnostic_reporter_v5_is_valid(&state),
        "recovery sequence must not go backwards"
    );
}

void test_strings_must_be_terminated() {
    diagnostic_reporter_state_v4_t state = make_v4_state();
    memset(
        state.interaction_events[0].detail_code,
        'a',
        sizeof(state.interaction_events[0].detail_code)
    );
    check(
        !diagnostic_reporter_v4_is_valid(&state),
        "unterminated interaction detail code must be rejected"
    );
}

void test_ack_keeps_newer_records() {
    diagnostic_reporter_state_t state = {};
    state.magic = DIAGNOSTIC_REPORTER_STATE_MAGIC;
    state.version = DIAGNOSTIC_REPORTER_STATE_VERSION;
    state.next_sequence = 6;
    state.boot_count = 1;
    state.source_boot_count = 1;
    state.boot_event_count = 2;
    state.boot_events[0] = make_boot_event(1);
    state.boot_events[1] = make_boot_event(2);
    state.failure = make_failure(3);
    state.recovery_event_count = 1;
    state.recovery_events[0] = make_recovery(4);
    state.interaction_event_count = 1;
    state.interaction_events[0] = make_interaction(
        5,
        "indicator_state",
        "listening"
    );

    check(
        diagnostic_reporter_acknowledge_state(&state, 3),
        "validated ACK compaction must succeed"
    );
    check(
        state.boot_event_count == 1 && state.boot_events[0].sequence == 2,
        "ack must retain newer boot records"
    );
    check(!state.failure.pending, "ack must mark the delivered failure");
    check(
        state.recovery_event_count == 1 &&
            state.recovery_events[0].sequence == 4,
        "ack must retain newer recovery records"
    );
    check(
        state.interaction_event_count == 1 &&
            state.interaction_events[0].sequence == 5,
        "ack must retain newer interaction records"
    );
    check(
        diagnostic_reporter_v5_is_valid(&state),
        "ack-compacted state must remain valid"
    );

    check(
        diagnostic_reporter_acknowledge_state(&state, 0),
        "zero acknowledgement is a no-op"
    );
    check(
        state.boot_event_count == 1 &&
            state.interaction_event_count == 1,
        "zero acknowledgement must not remove records"
    );
}

}  // namespace

int main() {
    test_v3_migration_preserves_history();
    test_v4_migration_preserves_interaction();
    test_wake_interaction_uses_zero_duration_and_ascii_detail();
    test_capacity_boundaries_are_bounded();
    test_sequences_must_be_strict_and_unique();
    test_strings_must_be_terminated();
    test_ack_keeps_newer_records();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("diagnostic_reporter_state tests passed");
    return 0;
}
