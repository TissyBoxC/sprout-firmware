#include "parent_control_decision.h"
#include "parent_policy.h"

#include <cassert>
#include <cstring>

static parent_policy_snapshot_t policy_with_categories(void) {
    parent_policy_snapshot_t policy = {};
    policy.policy_version = 3;
    policy.daily_limit_minutes = 30;
    policy.allowed_category_count = 1;
    strcpy(policy.allowed_categories[0], "story");
    policy.disabled_period_count = 1;
    strcpy(policy.disabled_periods[0].start_time, "22:00");
    strcpy(policy.disabled_periods[0].end_time, "06:00");
    return policy;
}

static void test_voice_gate_uses_daily_limit(void) {
    const parent_policy_snapshot_t policy = policy_with_categories();
    parent_control_decision_input_t input = {};
    input.policy = &policy;
    input.minute_of_day = 12 * 60;
    input.time_trusted = true;
    input.daily_limit_reached = true;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_DAILY_LIMIT_REACHED);
}

static void test_content_gate_uses_category(void) {
    const parent_policy_snapshot_t policy = policy_with_categories();
    parent_control_decision_input_t input = {};
    input.policy = &policy;
    input.category = "bedtime";
    input.minute_of_day = 12 * 60;
    input.time_trusted = true;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_CATEGORY_DENIED);

    input.category = "story";
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_DECISION_ALLOWED);
}

static void test_content_gate_uses_disabled_period(void) {
    const parent_policy_snapshot_t policy = policy_with_categories();
    parent_control_decision_input_t input = {};
    input.policy = &policy;
    input.category = "story";
    input.minute_of_day = 23 * 60;
    input.time_trusted = true;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_DISABLED_PERIOD);
}

// Mirrors the voice_session start gate: no category, daily limit enforced,
// and a missing policy denies closed.
static void test_voice_gate_denies_without_policy(void) {
    parent_control_decision_input_t input = {};
    input.category = nullptr;
    input.minute_of_day = 12 * 60;
    input.time_trusted = true;
    assert(parent_control_decision_denies(&input));
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_POLICY_UNAVAILABLE);
}

// Mirrors the playback queue gate: a safety-class item is always allowed, even
// inside a disabled period with the daily limit reached and an untrusted clock.
static void test_safety_lane_is_always_allowed(void) {
    const parent_policy_snapshot_t policy = policy_with_categories();
    parent_control_decision_input_t input = {};
    input.policy = &policy;
    input.category = "bedtime";
    input.minute_of_day = 23 * 60;
    input.time_trusted = false;
    input.daily_limit_reached = true;
    input.safety_exempt = true;
    assert(!parent_control_decision_denies(&input));
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_DECISION_ALLOWED);
}

// Mirrors the wake gate on a device whose clock is not yet trusted while a
// disabled period exists: the request must fail safe, not fail open.
static void test_wake_gate_fails_safe_when_time_untrusted(void) {
    const parent_policy_snapshot_t policy = policy_with_categories();
    parent_control_decision_input_t input = {};
    input.policy = &policy;
    input.category = nullptr;
    input.minute_of_day = 0;
    input.time_trusted = false;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_TIME_UNTRUSTED);
}

int main() {
    test_voice_gate_uses_daily_limit();
    test_content_gate_uses_category();
    test_content_gate_uses_disabled_period();
    test_voice_gate_denies_without_policy();
    test_safety_lane_is_always_allowed();
    test_wake_gate_fails_safe_when_time_untrusted();
    return 0;
}
