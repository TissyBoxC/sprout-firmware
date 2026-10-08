#include "parent_control_runtime.h"

#include <string.h>
#include <stdio.h>

#include "parent_control_decision.h"
#include "parent_policy.h"
#include "time_sync.h"
#include "usage_ledger.h"

static bool parent_control_runtime_ready;

static void parent_control_set_decision(
    parent_control_decision_t *decision_out,
    parent_control_reason_t reason,
    int64_t policy_version,
    bool policy_available
) {
    memset(decision_out, 0, sizeof(*decision_out));
    decision_out->reason = reason;
    decision_out->policy_version = policy_version;
    decision_out->policy_available = policy_available;
    const char *const name = parent_control_reason_name(reason);
    if (name != NULL) {
        snprintf(
            decision_out->reason_code,
            sizeof(decision_out->reason_code),
            "%s",
            name
        );
    }
}

static bool parent_control_time_is_valid(int64_t utc_epoch_seconds) {
    return utc_epoch_seconds > 0;
}

/**
 * @brief Resolve one decision, optionally recording blocked counters.
 *
 * Blocked counters describe real child attempts, so only the start path passes
 * record_blocked=true. Periodic continuation checks reuse the same decision
 * math without inflating the guardian-visible counters.
 */
static esp_err_t parent_control_resolve(
    const parent_control_request_t *request,
    bool record_blocked,
    parent_control_decision_t *decision_out
) {
    int32_t minute_of_day = 0;
    const bool time_trusted =
        parent_control_time_is_valid(request->utc_epoch_seconds) &&
        time_sync_local_minute_of_day(
            request->utc_epoch_seconds,
            request->timezone_offset_minutes,
            &minute_of_day
        ) == ESP_OK;
    if (!time_trusted) {
        minute_of_day = 0;
    }

    parent_policy_snapshot_t policy = {};
    const bool has_policy = parent_policy_get_snapshot(&policy) == ESP_OK;
    const bool daily_limit_reached =
        has_policy && policy.daily_limit_minutes > 0 &&
        usage_ledger_daily_limit_reached(policy.daily_limit_minutes);
    const parent_control_decision_input_t decision_input = {
        .policy = has_policy ? &policy : NULL,
        .category = request->category,
        .safety_exempt = request->safety_exempt,
        .minute_of_day = minute_of_day,
        .time_trusted = time_trusted,
        .daily_limit_reached = daily_limit_reached,
    };
    const parent_control_reason_t reason =
        parent_control_decision_evaluate(&decision_input);
    parent_control_set_decision(
        decision_out,
        reason,
        has_policy ? policy.policy_version : 0,
        has_policy
    );

    if (record_blocked && reason != PARENT_CONTROL_DECISION_ALLOWED) {
        const char *const reason_code = parent_control_reason_name(reason);
        if (reason_code != NULL) {
            (void)usage_ledger_record_blocked(
                request->utc_epoch_seconds,
                request->timezone_offset_minutes,
                reason_code
            );
        }
    }
    return ESP_OK;
}

esp_err_t parent_control_evaluate_norecord(
    const char *category,
    bool safety_exempt,
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    parent_control_decision_t *decision_out
) {
    if (!parent_control_runtime_ready || decision_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const parent_control_request_t request = {
        .category = category,
        .safety_exempt = safety_exempt,
        .utc_epoch_seconds = utc_epoch_seconds,
        .timezone_offset_minutes = timezone_offset_minutes,
    };
    return parent_control_resolve(&request, false, decision_out);
}

esp_err_t parent_control_runtime_init(void) {
    if (parent_control_runtime_ready) {
        return ESP_OK;
    }
    if (!parent_policy_is_ready() || !usage_ledger_is_ready() ||
        !time_sync_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    parent_control_runtime_ready = true;
    return ESP_OK;
}

bool parent_control_runtime_is_ready(void) {
    return parent_control_runtime_ready;
}

esp_err_t parent_control_evaluate(
    const parent_control_request_t *request,
    parent_control_decision_t *decision_out
) {
    // A consumption attempt records blocked counters so the guardian sees how
    // often the child was told no. Continuation checks must not, or a polling
    // caller would inflate the counters without a real new attempt.
    if (!parent_control_runtime_ready || request == NULL ||
        decision_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return parent_control_resolve(request, true, decision_out);
}

esp_err_t parent_control_evaluate_active(
    const char *category,
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    parent_control_decision_t *decision_out
) {
    // Continuation check: identical decision, but blocked counters are not
    // incremented because the activity was already counted when it started.
    return parent_control_evaluate_norecord(
        category,
        false,
        utc_epoch_seconds,
        timezone_offset_minutes,
        decision_out
    );
}

esp_err_t parent_control_note_conversation_finished(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    uint32_t conversation_seconds
) {
    return usage_ledger_record_conversation(
        utc_epoch_seconds,
        timezone_offset_minutes,
        conversation_seconds
    );
}

esp_err_t parent_control_note_content_finished(
    int64_t utc_epoch_seconds,
    int32_t timezone_offset_minutes,
    const char *category,
    uint32_t content_seconds
) {
    return usage_ledger_record_content_playback(
        utc_epoch_seconds,
        timezone_offset_minutes,
        category,
        content_seconds
    );
}

const char *parent_control_reason_name(parent_control_reason_t reason) {
    switch (reason) {
        case PARENT_CONTROL_DECISION_ALLOWED:
            return "allowed";
        case PARENT_CONTROL_REASON_DISABLED_PERIOD:
            return "PARENT_CONTROL_REASON_DISABLED_PERIOD";
        case PARENT_CONTROL_REASON_CATEGORY_DENIED:
            return "PARENT_CONTROL_REASON_CATEGORY_DENIED";
        case PARENT_CONTROL_REASON_DAILY_LIMIT_REACHED:
            return "PARENT_CONTROL_REASON_DAILY_LIMIT_REACHED";
        case PARENT_CONTROL_REASON_TIME_UNTRUSTED:
            return "PARENT_CONTROL_REASON_TIME_UNTRUSTED";
        case PARENT_CONTROL_REASON_POLICY_UNAVAILABLE:
            return "PARENT_CONTROL_REASON_POLICY_UNAVAILABLE";
        default:
            return "PARENT_CONTROL_REASON_INTERNAL";
    }
}

bool parent_control_decision_is_denied(
    const parent_control_decision_t *decision
) {
    return decision == NULL ||
           decision->reason != PARENT_CONTROL_DECISION_ALLOWED;
}

bool parent_control_request_is_safety_exempt(
    const parent_control_request_t *request
) {
    return request != NULL && request->safety_exempt;
}

void parent_control_runtime_shutdown(void) {
    parent_control_runtime_ready = false;
}

const module_descriptor_t *parent_control_runtime_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "parent_control_runtime",
        .version = "1.0.0",
        .initialize = parent_control_runtime_init,
        .shutdown = parent_control_runtime_shutdown,
    };
    return &descriptor;
}
