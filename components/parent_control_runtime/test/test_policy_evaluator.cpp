#include "parent_control_decision.h"
#include "parent_policy.h"

#include <cassert>
#include <cstring>

static parent_policy_snapshot_t make_policy(void) {
    parent_policy_snapshot_t policy = {};
    policy.policy_version = 7;
    policy.daily_limit_minutes = 60;
    policy.allowed_category_count = 2;
    strcpy(policy.allowed_categories[0], "story");
    strcpy(policy.allowed_categories[1], "bedtime");
    policy.disabled_period_count = 2;
    strcpy(policy.disabled_periods[0].start_time, "12:00");
    strcpy(policy.disabled_periods[0].end_time, "13:00");
    strcpy(policy.disabled_periods[1].start_time, "21:00");
    strcpy(policy.disabled_periods[1].end_time, "07:00");
    policy.max_volume_percent = 70;
    return policy;
}

static parent_control_decision_input_t base_input(
    const parent_policy_snapshot_t *policy
) {
    parent_control_decision_input_t input = {};
    input.policy = policy;
    input.category = "story";
    input.minute_of_day = 10 * 60;
    input.time_trusted = true;
    return input;
}

static void test_category_allow_and_deny(void) {
    const parent_policy_snapshot_t policy = make_policy();
    parent_control_decision_input_t input = base_input(&policy);
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_DECISION_ALLOWED);

    input.category = "english";
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_CATEGORY_DENIED);
}

static void test_disabled_period_boundaries(void) {
    const parent_policy_snapshot_t policy = make_policy();
    parent_control_decision_input_t input = base_input(&policy);

    input.minute_of_day = 11 * 60 + 59;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_DECISION_ALLOWED);
    input.minute_of_day = 12 * 60;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_DISABLED_PERIOD);
    input.minute_of_day = 12 * 60 + 59;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_DISABLED_PERIOD);
    input.minute_of_day = 13 * 60;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_DECISION_ALLOWED);
}

static void test_cross_midnight_period(void) {
    const parent_policy_snapshot_t policy = make_policy();
    parent_control_decision_input_t input = base_input(&policy);

    input.minute_of_day = 20 * 60 + 59;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_DECISION_ALLOWED);
    input.minute_of_day = 21 * 60;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_DISABLED_PERIOD);
    input.minute_of_day = 23 * 60 + 59;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_DISABLED_PERIOD);
    input.minute_of_day = 0;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_DISABLED_PERIOD);
    input.minute_of_day = 6 * 60 + 59;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_DISABLED_PERIOD);
    input.minute_of_day = 7 * 60;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_DECISION_ALLOWED);
}

static void test_daily_limit(void) {
    const parent_policy_snapshot_t policy = make_policy();
    parent_control_decision_input_t input = base_input(&policy);
    input.minute_of_day = 10 * 60;
    input.daily_limit_reached = true;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_DAILY_LIMIT_REACHED);
}

static void test_time_untrusted_fails_safe(void) {
    const parent_policy_snapshot_t policy = make_policy();
    parent_control_decision_input_t input = base_input(&policy);
    input.time_trusted = false;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_TIME_UNTRUSTED);
}

static void test_no_policy_denies(void) {
    parent_control_decision_input_t input = {};
    input.category = "story";
    input.time_trusted = true;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_POLICY_UNAVAILABLE);
}

static void test_safety_exempt(void) {
    const parent_policy_snapshot_t policy = make_policy();
    parent_control_decision_input_t input = base_input(&policy);
    input.safety_exempt = true;
    input.category = "english";
    input.minute_of_day = 12 * 60 + 30;
    input.time_trusted = false;
    input.daily_limit_reached = true;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_DECISION_ALLOWED);
}

static void test_disabled_period_without_time_is_untrusted_not_disabled(void) {
    const parent_policy_snapshot_t policy = make_policy();
    parent_control_decision_input_t input = base_input(&policy);
    // With no trusted minute of day, the evaluator must report the untrusted
    // clock rather than silently matching or skipping the disabled period.
    input.time_trusted = false;
    input.minute_of_day = 0;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_REASON_TIME_UNTRUSTED);
}

static void test_empty_category_is_not_category_denied(void) {
    const parent_policy_snapshot_t policy = make_policy();
    parent_control_decision_input_t input = base_input(&policy);
    // A category-less session (voice) must not be rejected by the category
    // allowlist; only a present, unknown category is denied.
    input.category = "";
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_DECISION_ALLOWED);
    input.category = nullptr;
    assert(parent_control_decision_evaluate(&input) ==
           PARENT_CONTROL_DECISION_ALLOWED);
}

int main() {
    test_category_allow_and_deny();
    test_disabled_period_boundaries();
    test_cross_midnight_period();
    test_daily_limit();
    test_time_untrusted_fails_safe();
    test_no_policy_denies();
    test_safety_exempt();
    test_disabled_period_without_time_is_untrusted_not_disabled();
    test_empty_category_is_not_category_denied();
    return 0;
}
