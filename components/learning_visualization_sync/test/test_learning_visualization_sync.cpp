#include "learning_visualization_sync.h"

#include <cassert>
#include <cstring>

#include "esp_http_client.h"
#include "learning_visualization.h"
#include "learning_visualization_sync_host.h"
#include "transport_security.h"

namespace {

void reset_all() {
    learning_visualization_sync_host_reset_module();
    learning_visualization_sync_host_reset();
    learning_visualization_sync_host_reset_store();
    learning_visualization_test_reset_ready();
    assert(learning_visualization_init() == ESP_OK);
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    learning_visualization_sync_host_set_http_sequence(
        statuses,
        bodies,
        1
    );
}

void set_apply_and_report_response(const char *policy_body) {
    const int statuses[] = {200, 200};
    const char *bodies[] = {policy_body, "{}"};
    learning_visualization_sync_host_set_http_sequence(
        statuses,
        bodies,
        2
    );
}

const char *enabled_policy_body() {
    return "{\"data\":{\"enabled\":true,\"feedback_style\":\"encouraging\","
           "\"animation_level\":\"full\",\"touch_feedback_enabled\":true,"
           "\"reward_effects_enabled\":true,"
           "\"lesson_linkage_enabled\":true,\"max_session_minutes\":30,"
           "\"revision\":7}}";
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        learning_visualization_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "learning_visualization_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_initialized_store() {
    learning_visualization_sync_host_reset();
    learning_visualization_sync_host_reset_module();
    learning_visualization_sync_host_reset_store();
    learning_visualization_test_reset_ready();
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_STATE);
    assert(learning_visualization_sync_host_request_count() == 0);
}

void test_now_requires_trusted_runtime_state() {
    reset_all();
    learning_visualization_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_IDLE
    );
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_STATE);

    learning_visualization_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_CONNECTED
    );
    learning_visualization_sync_host_set_time_synchronized(false);
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_STATE);

    learning_visualization_sync_host_set_time_synchronized(true);
    learning_visualization_sync_host_set_registered(false);
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_STATE);
    assert(learning_visualization_sync_host_request_count() == 0);
}

void test_now_rejects_unusable_identity_and_base_url() {
    reset_all();
    learning_visualization_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_STATE);
    assert(learning_visualization_sync_host_request_count() == 0);

    reset_all();
    learning_visualization_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_ARG);
    assert(learning_visualization_sync_host_request_count() == 0);

    reset_all();
    learning_visualization_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_ARG);
    assert(learning_visualization_sync_host_request_count() == 0);
}

void test_successful_fetch_applies_policy_and_reports_status() {
    reset_all();
    set_apply_and_report_response(enabled_policy_body());

    assert(learning_visualization_sync_now() == ESP_OK);
    assert(learning_visualization_sync_host_request_count() == 2);
    assert(
        strcmp(
            learning_visualization_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "learning-visualization/policy"
        ) == 0
    );
    assert(
        strcmp(
            learning_visualization_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        learning_visualization_sync_host_request_method(0) == HTTP_METHOD_GET
    );

    const learning_policy_t policy = learning_visualization_get();
    assert(policy.enabled == 1u);
    assert(policy.feedback_style == (uint8_t)LEARNING_STYLE_ENCOURAGING);
    assert(policy.animation_level == (uint8_t)LEARNING_ANIMATION_FULL);
    assert(policy.touch_feedback_enabled == 1u);
    assert(policy.reward_effects_enabled == 1u);
    assert(policy.lesson_linkage_enabled == 1u);
    assert(policy.max_session_minutes == 30u);
    assert(policy.revision == 7u);

    assert(
        strcmp(
            learning_visualization_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "learning-visualization/status"
        ) == 0
    );
    assert(
        learning_visualization_sync_host_request_method(1) == HTTP_METHOD_POST
    );
    assert(
        strcmp(
            learning_visualization_sync_host_request_content_type(1),
            "application/json"
        ) == 0
    );
    const char *report_body = learning_visualization_sync_host_request_body(1);
    assert(strstr(report_body, "\"feedback_style\":\"encouraging\"") != nullptr);
    // The policy allows full animation, but the surface is still idle, so the
    // effective animation reported to the platform is off.
    assert(strstr(report_body, "\"visual_state\":\"idle\"") != nullptr);
    assert(strstr(report_body, "\"animation_level\":\"off\"") != nullptr);
    assert(strstr(report_body, "\"policy_revision\":7") != nullptr);
}

void test_status_report_reflects_live_surface() {
    reset_all();
    learning_policy_t live = learning_visualization_get();
    live.enabled = 1u;
    live.feedback_style = (uint8_t)LEARNING_STYLE_PLAYFUL;
    live.animation_level = (uint8_t)LEARNING_ANIMATION_FULL;
    live.touch_feedback_enabled = 1u;
    live.reward_effects_enabled = 1u;
    live.lesson_linkage_enabled = 1u;
    live.max_session_minutes = 30u;
    live.revision = 3u;
    assert(learning_visualization_set(&live) == ESP_OK);
    assert(learning_visualization_begin_lesson("colors_basic") == ESP_OK);
    assert(learning_visualization_report_feedback() == ESP_OK);
    assert(learning_visualization_report_session_minutes(42u) == ESP_OK);

    set_apply_and_report_response(
        "{\"data\":{\"enabled\":true,\"feedback_style\":\"playful\","
        "\"animation_level\":\"full\",\"touch_feedback_enabled\":true,"
        "\"reward_effects_enabled\":true,\"lesson_linkage_enabled\":true,"
        "\"max_session_minutes\":30,\"revision\":9}}"
    );
    assert(learning_visualization_sync_now() == ESP_OK);

    const char *report_body = learning_visualization_sync_host_request_body(1);
    assert(strstr(report_body, "\"linked_lesson_id\":\"colors_basic\"") !=
           nullptr);
    assert(strstr(report_body, "\"visual_state\":\"feedback\"") != nullptr);
    assert(strstr(report_body, "\"feedback_events\":1") != nullptr);
    assert(strstr(report_body, "\"session_minutes\":42") != nullptr);
}

void test_disabled_policy_reports_idle_off_surface() {
    reset_all();
    set_apply_and_report_response(
        "{\"data\":{\"enabled\":false,\"feedback_style\":\"celebration\","
        "\"animation_level\":\"full\",\"touch_feedback_enabled\":true,"
        "\"reward_effects_enabled\":true,\"lesson_linkage_enabled\":true,"
        "\"max_session_minutes\":30,\"revision\":10}}"
    );

    assert(learning_visualization_sync_now() == ESP_OK);
    const learning_policy_t policy = learning_visualization_get();
    assert(policy.enabled == 0u);
    assert(policy.animation_level == (uint8_t)LEARNING_ANIMATION_OFF);
    assert(!learning_can_visualize(&policy));

    const char *report_body = learning_visualization_sync_host_request_body(1);
    assert(strstr(report_body, "\"visual_state\":\"idle\"") != nullptr);
    assert(strstr(report_body, "\"animation_level\":\"off\"") != nullptr);
    assert(strstr(report_body, "\"linked_lesson_id\":\"\"") != nullptr);
    assert(strstr(report_body, "\"feedback_events\":0") != nullptr);
    assert(strstr(report_body, "\"session_minutes\":0") != nullptr);
}

void test_malformed_responses_leave_policy_untouched() {
    reset_all();
    const learning_policy_t before = learning_visualization_get();

    set_single_response(200, "{not-json");
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_ARG);
    learning_policy_t after = learning_visualization_get();
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    set_single_response(200, "{}");
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_ARG);
    after = learning_visualization_get();
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    set_single_response(
        200,
        "{\"data\":{\"feedback_style\":\"loud\","
        "\"animation_level\":\"full\"}}"
    );
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_ARG);
    after = learning_visualization_get();
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    set_single_response(500, "{}");
    assert(learning_visualization_sync_now() == ESP_FAIL);
    after = learning_visualization_get();
    assert(memcmp(&before, &after, sizeof(before)) == 0);
}

void test_oversized_response_is_rejected() {
    reset_all();
    static char oversized[4200];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    const learning_policy_t before = learning_visualization_get();

    set_single_response(200, oversized);
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_ARG);
    assert(learning_visualization_sync_host_request_count() == 1);
    const learning_policy_t after = learning_visualization_get();
    assert(memcmp(&before, &after, sizeof(before)) == 0);
}

void test_unauthorized_fetch_clears_session() {
    reset_all();
    set_single_response(401, "{}");
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_STATE);
    assert(learning_visualization_sync_host_clear_session_calls() == 1);

    reset_all();
    set_single_response(403, "{}");
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_STATE);
    assert(learning_visualization_sync_host_clear_session_calls() == 1);
}

void test_transport_failures_are_propagated() {
    reset_all();
    learning_visualization_sync_host_set_client_init_fails(true);
    assert(learning_visualization_sync_now() == ESP_ERR_NO_MEM);

    reset_all();
    learning_visualization_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    learning_visualization_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(learning_visualization_sync_now() == ESP_ERR_INVALID_ARG);
    assert(learning_visualization_sync_host_request_count() == 0);
    assert(learning_visualization_sync_host_transport_security_calls() == 1);
}

void test_status_report_failure_does_not_fail_sync() {
    reset_all();
    const int statuses[] = {200, 500};
    const char *bodies[] = {enabled_policy_body(), "{}"};
    learning_visualization_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(learning_visualization_sync_now() == ESP_OK);
    assert(learning_visualization_sync_host_request_count() == 2);
    assert(learning_visualization_get().enabled == 1u);
}

void test_initialize_requires_store_and_is_idempotent() {
    learning_visualization_sync_host_reset();
    learning_visualization_sync_host_reset_module();
    learning_visualization_sync_host_reset_store();
    learning_visualization_test_reset_ready();
    const module_descriptor_t *descriptor =
        learning_visualization_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!learning_visualization_sync_host_is_ready());
    assert(learning_visualization_sync_host_task_create_calls() == 0);

    assert(learning_visualization_init() == ESP_OK);
    learning_visualization_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!learning_visualization_sync_host_is_ready());

    learning_visualization_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(learning_visualization_sync_host_is_ready());
    assert(learning_visualization_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(learning_visualization_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_with_short_interval() {
    reset_all();
    const module_descriptor_t *descriptor =
        learning_visualization_sync_module_descriptor();
    learning_visualization_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_IDLE
    );
    learning_visualization_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(learning_visualization_sync_host_task_pending());

    learning_visualization_sync_host_run_pending_task();
    assert(learning_visualization_sync_host_request_count() == 0);
    assert(learning_visualization_sync_host_delay_calls() == 1);
    assert(learning_visualization_sync_host_last_delay_ticks() == 15000u);
    assert(learning_visualization_sync_host_task_delete_calls() == 1);
    assert(!learning_visualization_sync_host_task_pending());
}

void test_worker_syncs_then_uses_regular_interval() {
    reset_all();
    const module_descriptor_t *descriptor =
        learning_visualization_sync_module_descriptor();
    set_apply_and_report_response(enabled_policy_body());
    learning_visualization_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    learning_visualization_sync_host_run_pending_task();
    assert(learning_visualization_sync_host_request_count() == 2);
    assert(learning_visualization_sync_host_delay_calls() == 1);
    assert(learning_visualization_sync_host_last_delay_ticks() == 3600000u);
    assert(learning_visualization_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_initialized_store();
    test_now_requires_trusted_runtime_state();
    test_now_rejects_unusable_identity_and_base_url();
    test_successful_fetch_applies_policy_and_reports_status();
    test_status_report_reflects_live_surface();
    test_disabled_policy_reports_idle_off_surface();
    test_malformed_responses_leave_policy_untouched();
    test_oversized_response_is_rejected();
    test_unauthorized_fetch_clears_session();
    test_transport_failures_are_propagated();
    test_status_report_failure_does_not_fail_sync();
    test_initialize_requires_store_and_is_idempotent();
    test_worker_retries_offline_with_short_interval();
    test_worker_syncs_then_uses_regular_interval();
    return 0;
}
