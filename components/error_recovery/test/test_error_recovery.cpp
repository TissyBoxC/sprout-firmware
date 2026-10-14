#include "error_recovery.h"

#include <cassert>
#include <cstring>

// Provided by the host stubs so this test can confirm the descriptor registers
// the recovery module as the registry failure/recovery observer.
module_failure_handler_t error_recovery_host_failure_handler(void);
module_recovery_handler_t error_recovery_host_recovery_handler(void);

// error_recovery keeps process-wide state with no reset hook, so this test
// drives one ordered sequence of failures and recoveries.
namespace {

void test_argument_validation() {
    assert(error_recovery_record(nullptr, ESP_FAIL) == ESP_ERR_INVALID_ARG);
    // A success code is not a failure and must not be recorded.
    assert(error_recovery_record("alpha", ESP_OK) == ESP_ERR_INVALID_ARG);
    // A name that sanitizes to empty is rejected.
    assert(error_recovery_record("", ESP_FAIL) == ESP_ERR_INVALID_ARG);
    assert(error_recovery_mark_recovered(nullptr) == ESP_ERR_INVALID_ARG);
    assert(error_recovery_mark_recovered("") == ESP_ERR_INVALID_ARG);

    const error_recovery_snapshot_t snapshot = error_recovery_get_snapshot();
    assert(snapshot.failure_count == 0u);
    assert(snapshot.module_name[0] == '\0');
}

void test_first_failure_is_recorded() {
    assert(error_recovery_record("network", ESP_ERR_TIMEOUT) == ESP_OK);

    const error_recovery_snapshot_t snapshot = error_recovery_get_snapshot();
    assert(std::strcmp(snapshot.module_name, "network") == 0);
    assert(snapshot.error_code == ESP_ERR_TIMEOUT);
    assert(snapshot.failure_count == 1u);
    assert(snapshot.transition_count == 1u);
}

void test_repeat_failure_increments_count_and_keeps_latest() {
    assert(error_recovery_record("network", ESP_ERR_TIMEOUT) == ESP_OK);
    assert(error_recovery_record("audio", ESP_FAIL) == ESP_OK);

    const error_recovery_snapshot_t snapshot = error_recovery_get_snapshot();
    assert(std::strcmp(snapshot.module_name, "audio") == 0);
    assert(snapshot.error_code == ESP_FAIL);
    // failure_count is cumulative across modules within one boot.
    assert(snapshot.failure_count == 3u);
    assert(snapshot.transition_count == 3u);
}

void test_module_names_are_sanitized() {
    // Control characters and non-printable bytes become underscores so a name
    // can never inject a line break into a log or a telemetry field.
    const char *dirty = "bad\nname\twith" "\x01" "control";
    assert(error_recovery_record(dirty, ESP_FAIL) == ESP_OK);

    const error_recovery_snapshot_t snapshot = error_recovery_get_snapshot();
    assert(std::strcmp(snapshot.module_name, "bad_name_with_control") == 0);
}

void test_overlong_module_name_is_truncated() {
    const char *long_name =
        "a_module_name_that_is_far_longer_than_the_bounded_field";
    assert(error_recovery_record(long_name, ESP_FAIL) == ESP_OK);

    const error_recovery_snapshot_t snapshot = error_recovery_get_snapshot();
    assert(
        std::strlen(snapshot.module_name) ==
        ERROR_RECOVERY_MODULE_NAME_SIZE - 1
    );
}

void test_recovery_is_appended_once_per_cycle() {
    assert(error_recovery_mark_recovered("audio") == ESP_OK);
    // A repeated recovery for the same module without a new failure is
    // idempotent and must not add a second event.
    assert(error_recovery_mark_recovered("audio") == ESP_OK);

    const error_recovery_recovery_snapshot_t recoveries =
        error_recovery_get_recoveries();
    assert(recoveries.event_count == 1u);
    assert(std::strcmp(recoveries.events[0].module_name, "audio") == 0);
}

void test_new_failure_closes_the_recovery_cycle() {
    // A failure after a recovery must let the next success add a new event.
    assert(error_recovery_record("audio", ESP_FAIL) == ESP_OK);
    assert(error_recovery_mark_recovered("audio") == ESP_OK);

    const error_recovery_recovery_snapshot_t recoveries =
        error_recovery_get_recoveries();
    assert(recoveries.event_count == 2u);
    assert(std::strcmp(recoveries.events[1].module_name, "audio") == 0);
    // Transition counts are strictly increasing within one boot.
    assert(
        recoveries.events[1].transition_count >
        recoveries.events[0].transition_count
    );
}

void test_recovery_queue_is_bounded_to_newest() {
    // Fill past the capacity; the oldest transitions must be evicted.
    for (int index = 0; index < ERROR_RECOVERY_RECOVERY_EVENT_CAPACITY + 4;
         ++index) {
        char name[ERROR_RECOVERY_MODULE_NAME_SIZE] = {0};
        name[0] = 'm';
        name[1] = (char)('0' + (index % 10));
        name[2] = (char)('a' + (index / 10));
        name[3] = '\0';
        assert(error_recovery_record(name, ESP_FAIL) == ESP_OK);
        assert(error_recovery_mark_recovered(name) == ESP_OK);
    }

    const error_recovery_recovery_snapshot_t recoveries =
        error_recovery_get_recoveries();
    assert(recoveries.event_count == ERROR_RECOVERY_RECOVERY_EVENT_CAPACITY);
    // The newest transition is retained and is the highest sequence seen.
    assert(
        recoveries.events[recoveries.event_count - 1].transition_count ==
        recoveries.latest_transition_count
    );
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        error_recovery_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "error_recovery") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    assert(descriptor->shutdown == nullptr);

    // Initialization must wire the module as the registry observer, otherwise
    // module failures would never reach diagnostics.
    assert(error_recovery_host_failure_handler() != nullptr);
    assert(error_recovery_host_recovery_handler() != nullptr);
}

}  // namespace

int main() {
    test_argument_validation();
    test_first_failure_is_recorded();
    test_repeat_failure_increments_count_and_keeps_latest();
    test_module_names_are_sanitized();
    test_overlong_module_name_is_truncated();
    test_recovery_is_appended_once_per_cycle();
    test_new_failure_closes_the_recovery_cycle();
    test_recovery_queue_is_bounded_to_newest();
    test_module_descriptor_is_stable();
    return 0;
}
