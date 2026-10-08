#include "provisioning_reporter_state.h"

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

provisioning_reporter_event_t make_event(
    uint32_t sequence,
    provisioning_event_type_t type,
    const char *detail_code
) {
    provisioning_reporter_event_t event = {};
    event.sequence = sequence;
    event.duration_ms = 0;
    snprintf(
        event.event_id,
        sizeof(event.event_id),
        "provisioning_%08lu",
        static_cast<unsigned long>(sequence)
    );
    snprintf(
        event.event_type,
        sizeof(event.event_type),
        "%s",
        provisioning_reporter_event_type_name(type)
    );
    snprintf(
        event.detail_code,
        sizeof(event.detail_code),
        "%s",
        detail_code
    );
    snprintf(
        event.firmware_version,
        sizeof(event.firmware_version),
        "%s",
        "0.7.0"
    );
    return event;
}

provisioning_reporter_state_t make_state() {
    provisioning_reporter_state_t state = {};
    state.magic = PROVISIONING_REPORTER_STATE_MAGIC;
    state.version = PROVISIONING_REPORTER_STATE_VERSION;
    state.next_sequence = 4;
    state.dropped = 1;
    state.wifi_configured = 1;
    state.last_provisioned_epoch = 1735689600;
    snprintf(
        state.last_detail_code,
        sizeof(state.last_detail_code),
        "%s",
        "credentials_accepted"
    );
    state.event_count = 3;
    state.events[0] = make_event(
        1,
        PROVISIONING_EVENT_PROVISIONING_STARTED,
        "first_boot"
    );
    state.events[1] = make_event(
        2,
        PROVISIONING_EVENT_WIFI_CONFIGURED,
        "stored"
    );
    state.events[2] = make_event(
        3,
        PROVISIONING_EVENT_NETWORK_RECONNECTED,
        "pending_512"
    );
    return state;
}

void test_event_type_contract() {
    const char *const expected[] = {
        "provisioning_started",
        "wifi_configured",
        "wifi_failed",
        "binding_completed",
        "binding_removed",
        "network_reconnected",
        "network_lost",
        "time_synced",
        "auth_revoked",
        "auth_restored",
        "binding_confirmed",
        "binding_pending",
    };
    check(
        sizeof(expected) / sizeof(expected[0]) ==
            static_cast<size_t>(PROVISIONING_EVENT_BINDING_PENDING) + 1,
        "event enum count"
    );
    for (size_t index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        check(
            strcmp(
                provisioning_reporter_event_type_name(
                    static_cast<provisioning_event_type_t>(index)
                ),
                expected[index]
            ) == 0,
            "event type mapping"
        );
    }
}

void test_state_validation_and_ack() {
    provisioning_reporter_state_t state = make_state();
    check(
        provisioning_reporter_state_is_valid(&state),
        "bounded state must be valid"
    );
    check(
        provisioning_reporter_acknowledge_state(&state, 2),
        "ack must succeed"
    );
    check(state.event_count == 1, "ack keeps newer events");
    check(state.events[0].sequence == 3, "newer event identity");
    check(
        strcmp(state.events[0].detail_code, "pending_512") == 0,
        "backlog detail code survives ack"
    );
    check(
        provisioning_reporter_acknowledge_state(&state, 0),
        "zero ack is a no-op"
    );
    check(state.event_count == 1, "zero ack must not drop records");
}

void test_invalid_state_is_rejected() {
    provisioning_reporter_state_t state = make_state();
    state.events[1].sequence = 1;
    check(
        !provisioning_reporter_state_is_valid(&state),
        "regressing sequences must be rejected"
    );

    state = make_state();
    snprintf(
        state.events[0].detail_code,
        sizeof(state.events[0].detail_code),
        "%s",
        "contains spaces"
    );
    check(
        !provisioning_reporter_state_is_valid(&state),
        "invalid detail code must be rejected"
    );

    state = make_state();
    state.event_count = PROVISIONING_REPORTER_EVENT_CAPACITY + 1;
    check(
        !provisioning_reporter_state_is_valid(&state),
        "over-capacity state must be rejected"
    );
}

void test_event_id_contract() {
    check(
        provisioning_reporter_event_id_is_valid("provisioning_00000001"),
        "event id must match the platform contract"
    );
    check(
        !provisioning_reporter_event_id_is_valid("Provisioning_1"),
        "event id must start with a lowercase letter"
    );
    check(
        !provisioning_reporter_event_id_is_valid("prov-event"),
        "event id must not contain punctuation"
    );
}

}  // namespace

int main() {
    test_event_type_contract();
    test_state_validation_and_ack();
    test_invalid_state_is_rejected();
    test_event_id_contract();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("provisioning_reporter_state tests passed");
    return 0;
}
