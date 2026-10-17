#include "video_session_sync.h"

#include <cassert>
#include <cstring>

#include "esp_http_client.h"
#include "network_manager.h"
#include "transport_security.h"
#include "video_session.h"
#include "video_session_sync_host.h"

namespace {

void reset_all() {
    video_session_test_reset_ready();
    video_session_sync_host_reset_module();
    video_session_sync_host_reset();
    video_init();
}

const char *const POLICY_BODY =
    "{\"data\":{"
    "\"schema_version\":1,"
    "\"enabled\":true,"
    "\"camera_allowed\":true,"
    "\"microphone_allowed\":true,"
    "\"require_child_consent\":false,"
    "\"quality_floor\":\"fair\","
    "\"ring_timeout_seconds\":45,"
    "\"max_session_seconds\":900,"
    "\"revision\":9"
    "}}";

void test_descriptor_is_stable() {
    const module_descriptor_t *const descriptor =
        video_session_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "video_session_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_ready_store() {
    video_session_test_reset_ready();
    video_session_sync_host_reset_module();
    video_session_sync_host_reset();
    assert(video_session_sync_now() == ESP_ERR_INVALID_STATE);
    assert(video_session_sync_host_request_count() == 0);
}

void test_fetch_rejects_unusable_runtime_state() {
    reset_all();
    video_session_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(video_session_sync_now() == ESP_ERR_INVALID_STATE);
    assert(video_session_sync_host_request_count() == 0);

    reset_all();
    video_session_sync_host_set_time_synchronized(false);
    assert(video_session_sync_now() == ESP_ERR_INVALID_STATE);
    assert(video_session_sync_host_request_count() == 0);

    reset_all();
    video_session_sync_host_set_registered(false);
    assert(video_session_sync_now() == ESP_ERR_INVALID_STATE);
    assert(video_session_sync_host_request_count() == 0);
}

void test_successful_fetch_applies_policy_and_reports_status() {
    reset_all();
    const int statuses[] = {200, 200};
    const char *bodies[] = {POLICY_BODY, "{}"};
    video_session_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(video_session_sync_now() == ESP_OK);
    assert(video_session_sync_host_request_count() == 2);
    assert(
        strcmp(
            video_session_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "video-session/policy"
        ) == 0
    );
    assert(video_session_sync_host_request_method(0) == HTTP_METHOD_GET);
    assert(
        strcmp(
            video_session_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        strcmp(
            video_session_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "video-session/status"
        ) == 0
    );
    assert(video_session_sync_host_request_method(1) == HTTP_METHOD_POST);
    assert(
        strcmp(
            video_session_sync_host_request_authorization(1),
            "Bearer session-1"
        ) == 0
    );

    const video_policy_t policy = video_get();
    assert(policy.enabled == 1u);
    assert(policy.camera_allowed == 1u);
    assert(policy.microphone_allowed == 1u);
    assert(policy.require_child_consent == 0u);
    assert(policy.quality_floor == VIDEO_QUALITY_FAIR);
    assert(policy.ring_timeout_seconds == 45u);
    assert(policy.max_session_seconds == 900u);
    assert(policy.revision == 9u);

    const char *const body = video_session_sync_host_request_body(1);
    assert(strstr(body, "\"session_supported\":true") != nullptr);
    assert(strstr(body, "\"camera_allowed\":true") != nullptr);
    assert(strstr(body, "\"microphone_allowed\":true") != nullptr);
    assert(strstr(body, "\"call_state\":\"idle\"") != nullptr);
    assert(strstr(body, "\"quality\":\"unknown\"") != nullptr);
    assert(strstr(body, "\"last_end_reason\":\"none\"") != nullptr);
    assert(strstr(body, "\"policy_revision\":9") != nullptr);
}

void test_status_report_uses_current_call_state() {
    reset_all();
    const int statuses[] = {200, 200, 200, 200};
    const char *bodies[] = {POLICY_BODY, "{}", POLICY_BODY, "{}"};
    video_session_sync_host_set_http_sequence(statuses, bodies, 4);
    assert(video_session_sync_now() == ESP_OK);
    assert(video_ring() == ESP_OK);
    assert(video_accept() == ESP_OK);
    assert(video_report_quality(VIDEO_QUALITY_GOOD) == ESP_OK);
    assert(video_end(VIDEO_END_HANGUP) == ESP_OK);

    assert(video_session_sync_now() == ESP_OK);
    assert(video_session_sync_host_request_count() == 4);
    const char *const body = video_session_sync_host_request_body(3);
    assert(strstr(body, "\"call_state\":\"ended\"") != nullptr);
    assert(strstr(body, "\"quality\":\"good\"") != nullptr);
    assert(strstr(body, "\"last_end_reason\":\"hangup\"") != nullptr);
}

void test_invalid_quality_floor_is_rejected_without_report() {
    reset_all();
    const int statuses[] = {200};
    const char *bodies[] = {
        "{\"data\":{"
        "\"enabled\":true,"
        "\"quality_floor\":\"ultra\","
        "\"ring_timeout_seconds\":45,"
        "\"max_session_seconds\":900,"
        "\"revision\":9"
        "}}",
    };
    video_session_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(video_session_sync_now() == ESP_ERR_INVALID_ARG);
    assert(video_session_sync_host_request_count() == 1);
    assert(video_get().revision == 0u);
}

void test_missing_data_is_rejected_without_report() {
    reset_all();
    const int statuses[] = {200};
    const char *bodies[] = {"{\"data\":{}}"};
    video_session_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(video_session_sync_now() == ESP_ERR_INVALID_ARG);
    assert(video_session_sync_host_request_count() == 1);
}

void test_unauthorized_clears_session() {
    reset_all();
    const int statuses[] = {401};
    const char *bodies[] = {"{}"};
    video_session_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(video_session_sync_now() == ESP_ERR_INVALID_STATE);
    assert(video_session_sync_host_clear_session_calls() == 1);

    reset_all();
    const int forbidden_statuses[] = {403};
    const char *forbidden_bodies[] = {"{}"};
    video_session_sync_host_set_http_sequence(
        forbidden_statuses,
        forbidden_bodies,
        1
    );
    assert(video_session_sync_now() == ESP_ERR_INVALID_STATE);
    assert(video_session_sync_host_clear_session_calls() == 1);
}

void test_identity_base_url_and_transport_failures() {
    reset_all();
    video_session_sync_host_set_copy_session_result(ESP_ERR_INVALID_STATE);
    assert(video_session_sync_now() == ESP_ERR_INVALID_STATE);
    assert(video_session_sync_host_request_count() == 0);

    reset_all();
    video_session_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(video_session_sync_now() == ESP_ERR_INVALID_STATE);
    assert(video_session_sync_host_request_count() == 0);

    reset_all();
    video_session_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(video_session_sync_now() == ESP_ERR_INVALID_SIZE);
    assert(video_session_sync_host_request_count() == 0);

    reset_all();
    video_session_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(video_session_sync_now() == ESP_ERR_INVALID_ARG);
    assert(video_session_sync_host_request_count() == 0);

    reset_all();
    video_session_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_HOST_NOT_ALLOWED
    );
    assert(video_session_sync_now() == ESP_ERR_INVALID_ARG);
    assert(video_session_sync_host_request_count() == 0);
    assert(video_session_sync_host_transport_security_calls() == 1);
}

void test_transport_and_server_failures_leave_policy_unchanged() {
    reset_all();
    const video_policy_t before = video_get();
    video_session_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(video_session_sync_now() == ESP_ERR_INVALID_STATE);
    assert(video_get().revision == before.revision);

    reset_all();
    const int statuses[] = {500};
    const char *bodies[] = {"{}"};
    video_session_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(video_session_sync_now() == ESP_FAIL);
    assert(video_session_sync_host_request_count() == 1);
}

void test_status_report_failure_propagates_after_policy_apply() {
    reset_all();
    const int statuses[] = {200, 500};
    const char *bodies[] = {POLICY_BODY, "{}"};
    video_session_sync_host_set_http_sequence(statuses, bodies, 2);
    assert(video_session_sync_now() == ESP_FAIL);
    assert(video_get().revision == 9u);
}

void test_unauthorized_status_report_clears_session() {
    reset_all();
    const int statuses[] = {200, 401};
    const char *bodies[] = {POLICY_BODY, "{}"};
    video_session_sync_host_set_http_sequence(statuses, bodies, 2);
    assert(video_session_sync_now() == ESP_ERR_INVALID_STATE);
    assert(video_get().revision == 9u);
    assert(video_session_sync_host_clear_session_calls() == 1);
}

void test_initialize_requires_store_and_is_idempotent() {
    video_session_test_reset_ready();
    video_session_sync_host_reset_module();
    video_session_sync_host_reset();
    const module_descriptor_t *const descriptor =
        video_session_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!video_session_sync_host_is_ready());
    assert(video_session_sync_host_task_create_calls() == 0);

    assert(video_init() == ESP_OK);
    video_session_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!video_session_sync_host_is_ready());

    video_session_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(video_session_sync_host_is_ready());
    assert(video_session_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(video_session_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_with_short_interval() {
    reset_all();
    const module_descriptor_t *const descriptor =
        video_session_sync_module_descriptor();
    video_session_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    video_session_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(video_session_sync_host_task_pending());

    video_session_sync_host_run_pending_task();
    assert(video_session_sync_host_request_count() == 0);
    assert(video_session_sync_host_delay_calls() == 1);
    assert(video_session_sync_host_last_delay_ticks() == 15000u);
    assert(video_session_sync_host_task_delete_calls() == 1);
    assert(!video_session_sync_host_task_pending());
}

void test_worker_syncs_then_uses_regular_interval() {
    reset_all();
    const module_descriptor_t *const descriptor =
        video_session_sync_module_descriptor();
    const int statuses[] = {200, 200};
    const char *bodies[] = {POLICY_BODY, "{}"};
    video_session_sync_host_set_http_sequence(statuses, bodies, 2);
    video_session_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    video_session_sync_host_run_pending_task();
    assert(video_session_sync_host_request_count() == 2);
    assert(video_session_sync_host_delay_calls() == 1);
    assert(video_session_sync_host_last_delay_ticks() == 3600000u);
    assert(video_session_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_ready_store();
    test_fetch_rejects_unusable_runtime_state();
    test_successful_fetch_applies_policy_and_reports_status();
    test_status_report_uses_current_call_state();
    test_invalid_quality_floor_is_rejected_without_report();
    test_missing_data_is_rejected_without_report();
    test_unauthorized_clears_session();
    test_identity_base_url_and_transport_failures();
    test_transport_and_server_failures_leave_policy_unchanged();
    test_status_report_failure_propagates_after_policy_apply();
    test_unauthorized_status_report_clears_session();
    test_initialize_requires_store_and_is_idempotent();
    test_worker_retries_offline_with_short_interval();
    test_worker_syncs_then_uses_regular_interval();
    return 0;
}
