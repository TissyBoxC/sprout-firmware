#include "ota_manager_state.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_happy_path_transitions() {
    ota_manager_state_t state = {};
    ota_manager_state_init(&state);
    check(
        ota_manager_state_transition(
            &state,
            OTA_MANAGER_STAGE_CHECKING,
            OTA_MANAGER_EVENT_STARTED
        ),
        "idle to checking must be allowed"
    );
    check(
        ota_manager_state_transition(
            &state,
            OTA_MANAGER_STAGE_DOWNLOADING,
            OTA_MANAGER_EVENT_STARTED
        ),
        "checking to downloading must be allowed"
    );
    check(
        ota_manager_state_transition(
            &state,
            OTA_MANAGER_STAGE_VALIDATING,
            OTA_MANAGER_EVENT_PROGRESS
        ),
        "downloading to validating must be allowed"
    );
    check(
        ota_manager_state_transition(
            &state,
            OTA_MANAGER_STAGE_INSTALLING,
            OTA_MANAGER_EVENT_VALIDATED
        ),
        "validating to installing must be allowed"
    );
    check(
        ota_manager_state_transition(
            &state,
            OTA_MANAGER_STAGE_PENDING_VERIFY,
            OTA_MANAGER_EVENT_INSTALLED
        ),
        "installing to pending verify must be allowed"
    );
    check(
        ota_manager_state_transition(
            &state,
            OTA_MANAGER_STAGE_VALID,
            OTA_MANAGER_EVENT_INSTALLED
        ),
        "pending verify to valid must be allowed"
    );
}

void test_invalid_transition_rejected() {
    ota_manager_state_t state = {};
    ota_manager_state_init(&state);
    check(
        !ota_manager_state_transition(
            &state,
            OTA_MANAGER_STAGE_INSTALLING,
            OTA_MANAGER_EVENT_VALIDATED
        ),
        "idle to installing must be rejected"
    );
    check(
        state.stage == OTA_MANAGER_STAGE_IDLE,
        "rejected transition must not change stage"
    );
}

void test_progress_and_event_consume() {
    ota_manager_state_t state = {};
    ota_manager_state_init(&state);
    (void)ota_manager_state_transition(
        &state,
        OTA_MANAGER_STAGE_CHECKING,
        OTA_MANAGER_EVENT_STARTED
    );
    check(
        ota_manager_state_consume_event(&state) ==
            OTA_MANAGER_EVENT_STARTED,
        "started event must be consumable"
    );
    check(
        ota_manager_state_consume_event(&state) ==
            OTA_MANAGER_EVENT_NONE,
        "event must be consumed exactly once"
    );
    ota_manager_state_set_progress(&state, 50, 100);
    check(
        ota_manager_state_consume_event(&state) ==
            OTA_MANAGER_EVENT_PROGRESS,
        "progress event must be consumable"
    );
}

void test_power_loss_recovery_reports_pending_verify() {
    ota_manager_state_t state = {};
    ota_manager_state_init(&state);
    state.stage = OTA_MANAGER_STAGE_PENDING_VERIFY;
    check(
        ota_manager_state_can_install(&state) == false,
        "pending verify image must not start another install"
    );
    (void)ota_manager_state_transition(
        &state,
        OTA_MANAGER_STAGE_VALID,
        OTA_MANAGER_EVENT_INSTALLED
    );
    check(
        state.stage == OTA_MANAGER_STAGE_VALID,
        "pending image must be able to become valid"
    );
}

void test_stage_names_are_stable() {
    check(
        std::strcmp(
            ota_manager_stage_name(OTA_MANAGER_STAGE_PENDING_VERIFY),
            "pending_verify"
        ) == 0,
        "pending verify stage name must be stable"
    );
    check(
        std::strcmp(
            ota_manager_event_name(OTA_MANAGER_EVENT_ROLLED_BACK),
            "rolled_back"
        ) == 0,
        "rolled back event name must be stable"
    );
}

}  // namespace

int main() {
    test_happy_path_transitions();
    test_invalid_transition_rejected();
    test_progress_and_event_consume();
    test_power_loss_recovery_reports_pending_verify();
    test_stage_names_are_stable();
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    std::puts("ota manager state tests passed");
    return 0;
}
