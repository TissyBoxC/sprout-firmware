#include "image_privacy_sync.h"

#include <cassert>
#include <cstring>

#include "esp_http_client.h"
#include "image_privacy.h"
#include "image_privacy_sync_host.h"
#include "transport_security.h"

namespace {

void reset_all() {
    image_privacy_sync_host_reset_module();
    image_privacy_sync_host_reset();
    image_privacy_sync_host_reset_store();
    image_privacy_test_reset_ready();
    assert(image_privacy_init() == ESP_OK);
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    image_privacy_sync_host_set_http_sequence(statuses, bodies, 1);
}

void set_apply_and_report_response(const char *policy_body) {
    const int statuses[] = {200, 200};
    const char *bodies[] = {policy_body, "{}"};
    image_privacy_sync_host_set_http_sequence(statuses, bodies, 2);
}

const char *enabled_policy_body() {
    return "{\"data\":{\"enabled\":true,\"upload_allowed\":true,"
           "\"local_processing_only\":false,\"processing_mode\":"
           "\"local_full\",\"upload_scope\":\"analysis\","
           "\"retention_days\":7,\"redaction_level\":\"standard\","
           "\"require_child_consent\":false,\"revision\":5}}";
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        image_privacy_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "image_privacy_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_initialized_store() {
    image_privacy_sync_host_reset();
    image_privacy_sync_host_reset_module();
    image_privacy_sync_host_reset_store();
    image_privacy_test_reset_ready();
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(image_privacy_sync_host_request_count() == 0);
}

void test_now_requires_trusted_runtime_state() {
    reset_all();
    image_privacy_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_STATE);

    image_privacy_sync_host_set_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    image_privacy_sync_host_set_time_synchronized(false);
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_STATE);

    image_privacy_sync_host_set_time_synchronized(true);
    image_privacy_sync_host_set_registered(false);
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(image_privacy_sync_host_request_count() == 0);
}

void test_now_rejects_unusable_identity_and_base_url() {
    reset_all();
    image_privacy_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(image_privacy_sync_host_request_count() == 0);

    reset_all();
    image_privacy_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(image_privacy_sync_host_request_count() == 0);

    reset_all();
    image_privacy_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(image_privacy_sync_host_request_count() == 0);
}

void test_successful_fetch_applies_policy_and_reports_status() {
    reset_all();
    set_apply_and_report_response(enabled_policy_body());

    assert(image_privacy_sync_now() == ESP_OK);
    assert(image_privacy_sync_host_request_count() == 2);
    assert(
        strcmp(
            image_privacy_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "image-privacy/policy"
        ) == 0
    );
    assert(
        strcmp(
            image_privacy_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        image_privacy_sync_host_request_method(0) == HTTP_METHOD_GET
    );

    const image_privacy_policy_t policy = image_privacy_get();
    assert(policy.enabled == 1u);
    assert(policy.upload_allowed == 1u);
    assert(policy.local_processing_only == 0u);
    assert(policy.processing_mode == (uint8_t)IMAGE_PROCESSING_LOCAL_FULL);
    assert(policy.upload_scope == (uint8_t)IMAGE_UPLOAD_ANALYSIS);
    assert(policy.retention_days == 7u);
    assert(policy.redaction_level == (uint8_t)IMAGE_REDACTION_STANDARD);
    assert(policy.require_child_consent == 0u);
    assert(policy.revision == 5u);

    assert(
        strcmp(
            image_privacy_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "image-privacy/status"
        ) == 0
    );
    assert(
        image_privacy_sync_host_request_method(1) == HTTP_METHOD_POST
    );
    assert(
        strcmp(
            image_privacy_sync_host_request_content_type(1),
            "application/json"
        ) == 0
    );
    const char *report_body = image_privacy_sync_host_request_body(1);
    assert(strstr(report_body, "\"processing_mode\":\"local_full\"") != nullptr);
    assert(strstr(report_body, "\"upload_scope\":\"analysis\"") != nullptr);
    assert(strstr(report_body, "\"redaction_level\":\"standard\"") != nullptr);
    assert(strstr(report_body, "\"policy_revision\":5") != nullptr);
}

void test_disabled_policy_reports_off_pipeline() {
    reset_all();
    set_apply_and_report_response(
        "{\"data\":{\"enabled\":false,\"upload_allowed\":true,"
        "\"local_processing_only\":false,\"processing_mode\":\"local_full\","
        "\"upload_scope\":\"analysis\",\"retention_days\":7,"
        "\"redaction_level\":\"standard\",\"require_child_consent\":false,"
        "\"revision\":6}}"
    );

    assert(image_privacy_sync_now() == ESP_OK);
    const image_privacy_policy_t policy = image_privacy_get();
    assert(policy.enabled == 0u);
    assert(policy.processing_mode == (uint8_t)IMAGE_PROCESSING_OFF);
    assert(policy.upload_allowed == 0u);
    assert(policy.upload_scope == (uint8_t)IMAGE_UPLOAD_NONE);
    assert(!image_privacy_can_process(&policy));
    assert(!image_privacy_can_upload(&policy));

    const char *report_body = image_privacy_sync_host_request_body(1);
    assert(strstr(report_body, "\"processing_mode\":\"off\"") != nullptr);
    assert(strstr(report_body, "\"upload_scope\":\"none\"") != nullptr);
    assert(strstr(report_body, "\"local_processing\":false") != nullptr);
    assert(strstr(report_body, "\"upload_allowed\":false") != nullptr);
}

void test_malformed_responses_leave_policy_untouched() {
    reset_all();
    const image_privacy_policy_t before = image_privacy_get();

    set_single_response(200, "{not-json");
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_ARG);
    image_privacy_policy_t after = image_privacy_get();
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    set_single_response(200, "{}");
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_ARG);
    after = image_privacy_get();
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    set_single_response(
        200,
        "{\"data\":{\"processing_mode\":\"cloud\","
        "\"upload_scope\":\"analysis\",\"redaction_level\":\"standard\"}}"
    );
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_ARG);
    after = image_privacy_get();
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    set_single_response(500, "{}");
    assert(image_privacy_sync_now() == ESP_FAIL);
    after = image_privacy_get();
    assert(memcmp(&before, &after, sizeof(before)) == 0);
}

void test_oversized_response_is_rejected() {
    reset_all();
    static char oversized[4200];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    const image_privacy_policy_t before = image_privacy_get();

    set_single_response(200, oversized);
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(image_privacy_sync_host_request_count() == 1);
    const image_privacy_policy_t after = image_privacy_get();
    assert(memcmp(&before, &after, sizeof(before)) == 0);
}

void test_unauthorized_fetch_clears_session() {
    reset_all();
    set_single_response(401, "{}");
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(image_privacy_sync_host_clear_session_calls() == 1);

    reset_all();
    set_single_response(403, "{}");
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(image_privacy_sync_host_clear_session_calls() == 1);
}

void test_transport_failures_are_propagated() {
    reset_all();
    image_privacy_sync_host_set_client_init_fails(true);
    assert(image_privacy_sync_now() == ESP_ERR_NO_MEM);

    reset_all();
    image_privacy_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    image_privacy_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(image_privacy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(image_privacy_sync_host_request_count() == 0);
    assert(image_privacy_sync_host_transport_security_calls() == 1);
}

void test_status_report_failure_does_not_fail_sync() {
    reset_all();
    const int statuses[] = {200, 500};
    const char *bodies[] = {enabled_policy_body(), "{}"};
    image_privacy_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(image_privacy_sync_now() == ESP_OK);
    assert(image_privacy_sync_host_request_count() == 2);
    assert(image_privacy_get().enabled == 1u);
}

void test_initialize_requires_store_and_is_idempotent() {
    image_privacy_sync_host_reset();
    image_privacy_sync_host_reset_module();
    image_privacy_sync_host_reset_store();
    image_privacy_test_reset_ready();
    const module_descriptor_t *descriptor =
        image_privacy_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!image_privacy_sync_host_is_ready());
    assert(image_privacy_sync_host_task_create_calls() == 0);

    assert(image_privacy_init() == ESP_OK);
    image_privacy_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!image_privacy_sync_host_is_ready());

    image_privacy_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(image_privacy_sync_host_is_ready());
    assert(image_privacy_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(image_privacy_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_with_short_interval() {
    reset_all();
    const module_descriptor_t *descriptor =
        image_privacy_sync_module_descriptor();
    image_privacy_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    image_privacy_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(image_privacy_sync_host_task_pending());

    image_privacy_sync_host_run_pending_task();
    assert(image_privacy_sync_host_request_count() == 0);
    assert(image_privacy_sync_host_delay_calls() == 1);
    assert(image_privacy_sync_host_last_delay_ticks() == 15000u);
    assert(image_privacy_sync_host_task_delete_calls() == 1);
    assert(!image_privacy_sync_host_task_pending());
}

void test_worker_syncs_then_uses_regular_interval() {
    reset_all();
    const module_descriptor_t *descriptor =
        image_privacy_sync_module_descriptor();
    set_apply_and_report_response(enabled_policy_body());
    image_privacy_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    image_privacy_sync_host_run_pending_task();
    assert(image_privacy_sync_host_request_count() == 2);
    assert(image_privacy_sync_host_delay_calls() == 1);
    assert(image_privacy_sync_host_last_delay_ticks() == 3600000u);
    assert(image_privacy_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_initialized_store();
    test_now_requires_trusted_runtime_state();
    test_now_rejects_unusable_identity_and_base_url();
    test_successful_fetch_applies_policy_and_reports_status();
    test_disabled_policy_reports_off_pipeline();
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
