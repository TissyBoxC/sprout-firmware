#include "parent_control_decision.h"

#include <string.h>

static int parent_control_decision_time_to_minutes(const char *value) {
    if (value == NULL || strlen(value) != 5 || value[2] != ':') {
        return -1;
    }
    return (value[0] - '0') * 600 +
           (value[1] - '0') * 60 +
           (value[3] - '0') * 10 +
           (value[4] - '0');
}

static bool parent_control_decision_period_matches(
    const parent_policy_disabled_period_t *period,
    int32_t minute_of_day
) {
    if (period == NULL) {
        return false;
    }
    const int start_minute =
        parent_control_decision_time_to_minutes(period->start_time);
    const int end_minute =
        parent_control_decision_time_to_minutes(period->end_time);
    if (start_minute < 0 || end_minute < 0 ||
        start_minute == end_minute) {
        return false;
    }
    if (start_minute < end_minute) {
        return minute_of_day >= start_minute && minute_of_day < end_minute;
    }
    // Cross-midnight window: 21:00-07:00 covers both late evening and early
    // morning phases of one local day.
    return minute_of_day >= start_minute || minute_of_day < end_minute;
}

static bool parent_control_decision_category_allowed(
    const parent_policy_snapshot_t *policy,
    const char *category
) {
    if (policy == NULL || category == NULL || category[0] == '\0') {
        return false;
    }
    for (size_t index = 0; index < policy->allowed_category_count; ++index) {
        if (strcmp(policy->allowed_categories[index], category) == 0) {
            return true;
        }
    }
    return false;
}

parent_control_reason_t parent_control_decision_evaluate(
    const parent_control_decision_input_t *input
) {
    if (input == NULL) {
        return PARENT_CONTROL_REASON_POLICY_UNAVAILABLE;
    }
    if (input->safety_exempt) {
        return PARENT_CONTROL_DECISION_ALLOWED;
    }
    if (input->policy == NULL) {
        return PARENT_CONTROL_REASON_POLICY_UNAVAILABLE;
    }
    if (input->policy->disabled_period_count > 0 &&
        !input->time_trusted) {
        return PARENT_CONTROL_REASON_TIME_UNTRUSTED;
    }
    for (size_t index = 0;
         index < input->policy->disabled_period_count;
         ++index) {
        if (parent_control_decision_period_matches(
                &input->policy->disabled_periods[index],
                input->minute_of_day
            )) {
            return PARENT_CONTROL_REASON_DISABLED_PERIOD;
        }
    }
    if (input->category != NULL && input->category[0] != '\0' &&
        !parent_control_decision_category_allowed(
            input->policy,
            input->category
        )) {
        return PARENT_CONTROL_REASON_CATEGORY_DENIED;
    }
    if (input->daily_limit_reached) {
        return PARENT_CONTROL_REASON_DAILY_LIMIT_REACHED;
    }
    return PARENT_CONTROL_DECISION_ALLOWED;
}

bool parent_control_decision_denies(
    const parent_control_decision_input_t *input
) {
    return parent_control_decision_evaluate(input) !=
           PARENT_CONTROL_DECISION_ALLOWED;
}
