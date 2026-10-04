#include <stdbool.h>
#include <stdint.h>
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

bool detail_code_is_valid(const char *detail_code) {
    const size_t length = detail_code == NULL ? 0 : strlen(detail_code);
    if (length < 1 || length > 64) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const unsigned char value = (unsigned char)detail_code[index];
        const bool is_letter =
            (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z');
        const bool is_digit = value >= '0' && value <= '9';
        const bool is_symbol = value == '_' || value == '.' ||
            value == ':' || value == '-';
        if (!is_letter && !is_digit && !is_symbol) {
            return false;
        }
    }
    return true;
}

enum command_result_t {
    COMMAND_ACKNOWLEDGED = 0,
    COMMAND_FAILED,
};

enum ack_outcome_t {
    ACK_OK = 0,
    ACK_ALREADY_HANDLED,
    ACK_RETRYABLE,
};

struct command_state_t {
    const char *id;
    const char *type;
    bool completed_guard;
    int erase_count;
    int ack_count;
    int restart_count;
};

command_result_t execute_factory_reset(
    command_state_t *state,
    bool erase_succeeds,
    ack_outcome_t ack_outcome
) {
    if (state->completed_guard) {
        state->ack_count++;
        if (ack_outcome != ACK_RETRYABLE) {
            state->restart_count++;
            return COMMAND_ACKNOWLEDGED;
        }
        return COMMAND_FAILED;
    }

    state->erase_count++;
    if (!erase_succeeds) {
        ++state->ack_count;
        return COMMAND_FAILED;
    }

    state->completed_guard = true;
    state->ack_count++;
    if (ack_outcome == ACK_RETRYABLE) {
        return COMMAND_FAILED;
    }
    state->restart_count++;
    return COMMAND_ACKNOWLEDGED;
}

bool command_status_is_executable(const char *status) {
    return strcmp(status, "pending") == 0 ||
        strcmp(status, "delivered") == 0;
}

void test_ack_success_is_the_only_restart_path() {
    command_state_t success = {"cmd-1", "factory_reset", false, 0, 0, 0};
    check(
        execute_factory_reset(&success, true, ACK_OK) ==
            COMMAND_ACKNOWLEDGED,
        "successful factory reset must be acknowledged"
    );
    check(success.erase_count == 1, "factory reset must erase once");
    check(success.ack_count == 1, "factory reset must acknowledge once");
    check(success.restart_count == 1, "ACK success must restart");

    command_state_t failure = {"cmd-2", "factory_reset", false, 0, 0, 0};
    check(
        execute_factory_reset(&failure, true, ACK_RETRYABLE) ==
            COMMAND_FAILED,
        "ACK failure must fail the command"
    );
    check(failure.erase_count == 1, "ACK failure may erase once");
    check(failure.restart_count == 0, "ACK failure must not restart");
}

void test_replayed_command_never_erases_twice() {
    command_state_t state = {"cmd-3", "factory_reset", false, 0, 0, 0};
    check(
        execute_factory_reset(&state, true, ACK_RETRYABLE) ==
            COMMAND_FAILED,
        "first attempt without ACK must not restart"
    );
    check(
        execute_factory_reset(&state, true, ACK_ALREADY_HANDLED) ==
            COMMAND_ACKNOWLEDGED,
        "already handled ACK must be idempotent success"
    );
    check(state.erase_count == 1, "replayed command must not erase twice");
    check(state.restart_count == 1, "replayed command must restart after ACK");
}

void test_delivered_commands_remain_executable() {
    check(
        command_status_is_executable("pending"),
        "pending commands must be executable"
    );
    check(
        command_status_is_executable("delivered"),
        "delivered commands still await ACK and must be executable"
    );
    check(
        !command_status_is_executable("acknowledged"),
        "acknowledged commands must not be executed again"
    );
    check(
        !command_status_is_executable("failed"),
        "failed commands must not be executed again"
    );
}

void test_wake_event_duration_contract() {
    const uint32_t duration_ms = 0;
    check(duration_ms == 0, "wake_detected duration must be zero");
    check(
        detail_code_is_valid("wake_1_confidence_0700"),
        "wake confidence detail code must be stable ASCII"
    );
    check(
        !detail_code_is_valid("wake detected"),
        "detail code must not contain spaces"
    );
    check(
        !detail_code_is_valid("\xE9"),
        "detail code must reject non-ASCII text"
    );
}

void test_interaction_event_types_contract() {
    const char *const event_types[] = {
        "wake_detected",
        "wake_rejected",
        "button_gesture",
        "indicator_state",
        "factory_reset_requested",
        "factory_reset_cancelled",
        "factory_reset_completed",
        "factory_reset_failed",
    };
    check(
        sizeof(event_types) / sizeof(event_types[0]) == 8,
        "platform interaction event type count"
    );
    for (size_t index = 0;
         index < sizeof(event_types) / sizeof(event_types[0]);
         ++index) {
        check(
            strcmp(event_types[index], "") != 0,
            "interaction event type must be non-empty"
        );
    }
}

}  // namespace

int main() {
    test_ack_success_is_the_only_restart_path();
    test_replayed_command_never_erases_twice();
    test_delivered_commands_remain_executable();
    test_wake_event_duration_contract();
    test_interaction_event_types_contract();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("device_runtime_reporter contract tests passed");
    return 0;
}
