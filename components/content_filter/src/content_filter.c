#include "content_filter.h"

#include "esp_log.h"
#include "content_filter_core.h"

static const char *const TAG = "content_filter";

static bool content_filter_ready;

content_filter_action_t content_filter_evaluate(
    content_filter_direction_t direction,
    const char *text,
    content_filter_decision_t *decision_out
) {
    return content_filter_core_evaluate(direction, text, decision_out);
}

const char *content_filter_action_name(content_filter_action_t action) {
    switch (action) {
        case CONTENT_FILTER_ACTION_ALLOW:
            return "allow";
        case CONTENT_FILTER_ACTION_CRISIS_INTERVENTION:
            return "crisis_intervention";
        case CONTENT_FILTER_ACTION_BLOCK:
            return "block";
        default:
            return "unknown";
    }
}

const char *content_filter_reason_name(content_filter_reason_t reason) {
    switch (reason) {
        case CONTENT_FILTER_REASON_OK:
            return "ok";
        case CONTENT_FILTER_REASON_NULL_ARGUMENT:
            return "null_argument";
        case CONTENT_FILTER_REASON_INVALID_DIRECTION:
            return "invalid_direction";
        case CONTENT_FILTER_REASON_EMPTY_TEXT:
            return "empty_text";
        case CONTENT_FILTER_REASON_TEXT_TOO_LONG:
            return "text_too_long";
        case CONTENT_FILTER_REASON_PROMPT_INJECTION:
            return "prompt_injection";
        case CONTENT_FILTER_REASON_VIOLENCE:
            return "violence";
        case CONTENT_FILTER_REASON_SEXUAL:
            return "sexual";
        case CONTENT_FILTER_REASON_HORROR:
            return "horror";
        case CONTENT_FILTER_REASON_ILLEGAL:
            return "illegal";
        case CONTENT_FILTER_REASON_GAMBLING:
            return "gambling";
        case CONTENT_FILTER_REASON_DRUGS:
            return "drugs";
        case CONTENT_FILTER_REASON_PERSONAL_DATA_REQUEST:
            return "personal_data_request";
        case CONTENT_FILTER_REASON_OFFLINE_MEETING:
            return "offline_meeting";
        case CONTENT_FILTER_REASON_EXTERNAL_LINK:
            return "external_link";
        case CONTENT_FILTER_REASON_COMMERCIAL_INDUCEMENT:
            return "commercial_inducement";
        case CONTENT_FILTER_REASON_CRISIS_SELF_HARM:
            return "crisis_self_harm";
        case CONTENT_FILTER_REASON_CRISIS_ABUSE:
            return "crisis_abuse";
        case CONTENT_FILTER_REASON_CRISIS_METHOD:
            return "crisis_method";
        default:
            return "unknown";
    }
}

esp_err_t content_filter_init(void) {
    if (content_filter_ready) {
        return ESP_OK;
    }
    content_filter_ready = true;
    ESP_LOGI(TAG, "child content filter ready");
    return ESP_OK;
}

bool content_filter_is_ready(void) {
    return content_filter_ready;
}

const module_descriptor_t *content_filter_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "content_filter",
        .version = "1.0.0",
        .initialize = content_filter_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
