#include "ota_manager_event_sequence.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_success_sequence_is_canonical() {
    const char *events[7] = {};
    const char *const sequence[] = {
        "started",
        "downloading",
        "downloaded",
        "validated",
        "installing",
        "pending_verify",
        "succeeded",
    };
    for (size_t index = 0; index < 7; ++index) {
        check(
            ota_event_sequence_append(
                events,
                index,
                7,
                sequence[index]
            ) == OTA_EVENT_SEQUENCE_OK,
            "canonical success event must be accepted"
        );
    }
    check(
        ota_event_sequence_is_success(events, 7),
        "full success sequence must be accepted"
    );
    check(
        ota_event_sequence_is_terminal(events, 7),
        "succeeded must be terminal"
    );
}

void test_out_of_order_event_is_rejected() {
    const char *events[2] = {};
    check(
        ota_event_sequence_append(events, 0, 2, "downloaded") ==
            OTA_EVENT_SEQUENCE_NOT_FIRST,
        "downloaded must not start the sequence"
    );
    check(
        ota_event_sequence_append(events, 0, 2, "started") ==
            OTA_EVENT_SEQUENCE_OK,
        "started must begin the sequence"
    );
    check(
        ota_event_sequence_append(events, 1, 2, "validated") ==
            OTA_EVENT_SEQUENCE_INVALID_TRANSITION,
        "skipping downloading must be rejected"
    );
}

void test_install_failure_and_rollback_sequences() {
    const char *events[7] = {};
    const char *const prefix[] = {
        "started",
        "downloading",
        "downloaded",
        "validated",
        "installing",
        "pending_verify",
    };
    for (size_t index = 0; index < 6; ++index) {
        check(
            ota_event_sequence_append(
                events,
                index,
                7,
                prefix[index]
            ) == OTA_EVENT_SEQUENCE_OK,
            "failure prefix must be accepted"
        );
    }
    check(
        ota_event_sequence_append(events, 6, 7, "failed") ==
            OTA_EVENT_SEQUENCE_OK,
        "pending verify fault must allow failed"
    );
    check(
        ota_event_sequence_is_terminal(events, 7),
        "failed must be terminal"
    );

    const char *rollback[8] = {};
    for (size_t index = 0; index < 6; ++index) {
        check(
            ota_event_sequence_append(
                rollback,
                index,
                8,
                prefix[index]
            ) == OTA_EVENT_SEQUENCE_OK,
            "rollback prefix must be accepted"
        );
    }
    check(
        ota_event_sequence_append(rollback, 6, 8, "rollback_started") ==
            OTA_EVENT_SEQUENCE_OK,
        "pending verify may start rollback"
    );
    check(
        ota_event_sequence_append(rollback, 7, 8, "rolled_back") ==
            OTA_EVENT_SEQUENCE_OK,
        "rollback start must allow terminal rolled_back"
    );
    check(
        ota_event_sequence_is_terminal(rollback, 8),
        "rolled_back must be terminal"
    );
    check(
        ota_event_sequence_append(rollback, 8, 8, "started") ==
            OTA_EVENT_SEQUENCE_INVALID_TRANSITION,
        "terminal sequence must reject overflow"
    );
}

void test_unknown_event_is_rejected() {
    const char *events[1] = {};
    check(
        ota_event_sequence_append(events, 0, 1, "progress") ==
            OTA_EVENT_SEQUENCE_INVALID_ARGUMENT,
        "legacy progress event must not be emitted"
    );
}

void test_terminal_event_may_follow_device_restart() {
    const char *events[1] = {};
    check(
        ota_event_sequence_append(events, 0, 1, "succeeded") ==
            OTA_EVENT_SEQUENCE_OK,
        "restarted image may report succeeded as first event"
    );
    check(
        ota_event_sequence_is_terminal(events, 1),
        "restarted succeeded event must be terminal"
    );

    const char *failed_events[1] = {};
    check(
        ota_event_sequence_append(failed_events, 0, 1, "failed") ==
            OTA_EVENT_SEQUENCE_OK,
        "restarted image may report failure as first event"
    );
}

}  // namespace

int main() {
    test_success_sequence_is_canonical();
    test_out_of_order_event_is_rejected();
    test_install_failure_and_rollback_sequences();
    test_unknown_event_is_rejected();
    test_terminal_event_may_follow_device_restart();
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::puts("ota manager event sequence tests passed");
    return 0;
}
