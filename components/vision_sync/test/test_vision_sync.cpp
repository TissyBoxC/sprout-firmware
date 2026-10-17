#include "vision_sync.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "camera_vision.h"
#include "esp_http_client.h"
#include "network_manager.h"
#include "vision_sync_host.h"

namespace {

constexpr const char *kTaskId = "vision-task-0001";

void reset_all() {
    vision_sync_host_reset_module();
    vision_sync_host_clear_provider();
    vision_sync_host_reset();
}

const char *pending_body_with_task(
    const char *task_id = kTaskId,
    const char *task_type = "object_recognition",
    const char *status = "pending",
    const char *prompt = "what is this"
) {
    static char body[2048];
    snprintf(
        body,
        sizeof(body),
        "{\"data\":{\"tasks\":[{"
        "\"task_id\":\"%s\","
        "\"task_type\":\"%s\","
        "\"status\":\"%s\","
        "\"prompt\":\"%s\""
        "}]}}",
        task_id,
        task_type,
        status,
        prompt
    );
    return body;
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    vision_sync_host_set_http_sequence(statuses, bodies, 1);
}

void set_closed_loop_responses(const char *pending_body) {
    const int statuses[] = {200, 200, 201};
    const char *bodies[] = {
        pending_body,
        "{\"data\":{\"task_id\":\"vision-task-0001\"}}",
        "{\"data\":{\"task_id\":\"vision-task-0001\"}}",
    };
    vision_sync_host_set_http_sequence(statuses, bodies, 3);
}

esp_err_t successful_provider(
    const char *task_id,
    camera_vision_task_type_t task_type,
    const char *prompt,
    void *context,
    vision_sync_result_t *result_out
) {
    (void)task_type;
    (void)context;
    assert(task_id != nullptr);
    assert(prompt != nullptr);
    assert(std::strlen(task_id) > 0);
    assert(std::strlen(task_id) < 64);
    assert(std::strcmp(prompt, "what is this") == 0);
    result_out->frame_width = 640;
    result_out->frame_height = 480;
    result_out->frame_bytes = 122880;
    std::strncpy(
        result_out->summary,
        "a red ball",
        sizeof(result_out->summary) - 1
    );
    result_out->label_count = 1;
    std::strncpy(
        result_out->labels[0].label,
        "ball",
        sizeof(result_out->labels[0].label) - 1
    );
    result_out->labels[0].confidence = 9000;
    return ESP_OK;
}

esp_err_t invalid_result_provider(
    const char *task_id,
    camera_vision_task_type_t task_type,
    const char *prompt,
    void *context,
    vision_sync_result_t *result_out
) {
    (void)task_id;
    (void)task_type;
    (void)prompt;
    (void)context;
    *result_out = {};
    result_out->frame_width = 5000;
    result_out->frame_height = 480;
    result_out->frame_bytes = 1024;
    return ESP_OK;
}

esp_err_t timeout_provider(
    const char *task_id,
    camera_vision_task_type_t task_type,
    const char *prompt,
    void *context,
    vision_sync_result_t *result_out
) {
    (void)task_id;
    (void)task_type;
    (void)prompt;
    (void)context;
    (void)result_out;
    return ESP_ERR_TIMEOUT;
}

void test_descriptor_is_stable() {
    const module_descriptor_t *const descriptor =
        vision_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "vision_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_camera_runtime() {
    reset_all();
    vision_sync_host_set_camera_ready(false);
    assert(vision_sync_now() == ESP_ERR_INVALID_STATE);
    assert(vision_sync_host_request_count() == 0);
}

void test_unusable_runtime_state_does_not_send_requests() {
    reset_all();
    vision_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(vision_sync_now() == ESP_ERR_INVALID_STATE);
    assert(vision_sync_host_request_count() == 0);

    reset_all();
    vision_sync_host_set_time_synchronized(false);
    assert(vision_sync_now() == ESP_ERR_INVALID_STATE);
    assert(vision_sync_host_request_count() == 0);

    reset_all();
    vision_sync_host_set_registered(false);
    assert(vision_sync_now() == ESP_ERR_INVALID_STATE);
    assert(vision_sync_host_request_count() == 0);
}

void test_prepare_propagates_identity_and_base_url_failures() {
    reset_all();
    vision_sync_host_set_copy_session_result(ESP_ERR_INVALID_STATE);
    assert(vision_sync_now() == ESP_ERR_INVALID_STATE);
    assert(vision_sync_host_request_count() == 0);

    reset_all();
    vision_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(vision_sync_now() == ESP_ERR_INVALID_SIZE);
    assert(vision_sync_host_request_count() == 0);

    reset_all();
    vision_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(vision_sync_now() == ESP_ERR_INVALID_SIZE);
    assert(vision_sync_host_request_count() == 0);

    reset_all();
    vision_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(vision_sync_now() == ESP_ERR_INVALID_ARG);
    assert(vision_sync_host_request_count() == 0);
}

void test_empty_queue_is_success_without_lifecycle_posts() {
    reset_all();
    set_single_response(200, "{\"data\":{\"tasks\":[]}}");
    assert(vision_sync_now() == ESP_OK);
    assert(vision_sync_host_request_count() == 1);
    assert(vision_sync_host_request_method(0) == HTTP_METHOD_GET);
    assert(
        std::strcmp(
            vision_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "vision/tasks/pending"
        ) == 0
    );
}

void test_successful_result_closes_task_loop() {
    reset_all();
    const vision_sync_provider_t provider = {
        .analyze = successful_provider,
        .context = nullptr,
    };
    assert(vision_sync_register_provider(&provider) == ESP_OK);
    set_closed_loop_responses(pending_body_with_task());

    assert(vision_sync_now() == ESP_OK);
    assert(vision_sync_host_request_count() == 3);

    assert(
        std::strcmp(
            vision_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "vision/tasks/pending"
        ) == 0
    );
    assert(
        std::strcmp(
            vision_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "vision/tasks/vision-task-0001/acknowledge"
        ) == 0
    );
    assert(
        std::strcmp(
            vision_sync_host_request_url(2),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "vision/results"
        ) == 0
    );

    assert(
        std::strcmp(
            vision_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        std::strcmp(
            vision_sync_host_request_authorization(1),
            "Bearer session-1"
        ) == 0
    );
    assert(
        std::strcmp(
            vision_sync_host_request_authorization(2),
            "Bearer session-1"
        ) == 0
    );
    assert(
        std::strcmp(
            vision_sync_host_request_accept(2),
            "application/json"
        ) == 0
    );
    assert(
        std::strcmp(
            vision_sync_host_request_content_type(2),
            "application/json"
        ) == 0
    );

    const char *const result_body = vision_sync_host_request_body(2);
    assert(std::strstr(result_body, "\"task_id\":\"vision-task-0001\"") != nullptr);
    assert(std::strstr(result_body, "\"frame_width\":640") != nullptr);
    assert(std::strstr(result_body, "\"summary\":\"a red ball\"") != nullptr);
    assert(std::strstr(result_body, "\"label\":\"ball\"") != nullptr);

    assert(vision_sync_host_camera_accept_calls() == 1);
    assert(vision_sync_host_camera_acknowledge_calls() == 1);
    assert(vision_sync_host_camera_fail_calls() == 0);
}

void test_no_provider_reports_unsupported_failure() {
    reset_all();
    const int statuses[] = {200, 200, 200};
    const char *bodies[] = {
        pending_body_with_task(),
        "{}",
        "{}",
    };
    vision_sync_host_set_http_sequence(statuses, bodies, 3);

    assert(vision_sync_now() == ESP_OK);
    assert(vision_sync_host_request_count() == 3);
    assert(
        std::strcmp(
            vision_sync_host_request_url(2),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "vision/tasks/vision-task-0001/status"
        ) == 0
    );
    const char *const status_body = vision_sync_host_request_body(2);
    assert(std::strstr(status_body, "\"status\":\"failed\"") != nullptr);
    assert(std::strstr(status_body, "\"failure_code\":\"unsupported\"") != nullptr);
    assert(vision_sync_host_camera_fail_calls() == 1);
    assert(
        std::strcmp(vision_sync_host_last_failure_code(), "unsupported") == 0
    );
}

void test_consent_failure_reports_stable_code() {
    reset_all();
    const vision_sync_provider_t provider = {
        .analyze = successful_provider,
        .context = nullptr,
    };
    assert(vision_sync_register_provider(&provider) == ESP_OK);
    vision_sync_host_set_accept_result(
        CAMERA_VISION_ERR_PRIVACY_CONSENT_REQUIRED
    );
    const int statuses[] = {200, 200, 200};
    const char *bodies[] = {pending_body_with_task(), "{}", "{}"};
    vision_sync_host_set_http_sequence(statuses, bodies, 3);

    assert(vision_sync_now() == ESP_OK);
    assert(vision_sync_host_request_count() == 3);
    assert(
        std::strstr(
            vision_sync_host_request_body(2),
            "\"failure_code\":\"consent_revoked\""
        ) != nullptr
    );
    assert(vision_sync_host_camera_accept_calls() == 1);
    assert(vision_sync_host_camera_fail_calls() == 0);
}

void test_provider_failure_and_invalid_result_report_bounded_codes() {
    reset_all();
    const vision_sync_provider_t timeout = {
        .analyze = timeout_provider,
        .context = nullptr,
    };
    assert(vision_sync_register_provider(&timeout) == ESP_OK);
    const int statuses[] = {200, 200, 200};
    const char *bodies[] = {pending_body_with_task(), "{}", "{}"};
    vision_sync_host_set_http_sequence(statuses, bodies, 3);

    assert(vision_sync_now() == ESP_OK);
    assert(
        std::strstr(
            vision_sync_host_request_body(2),
            "\"failure_code\":\"timeout\""
        ) != nullptr
    );

    reset_all();
    const vision_sync_provider_t invalid = {
        .analyze = invalid_result_provider,
        .context = nullptr,
    };
    assert(vision_sync_register_provider(&invalid) == ESP_OK);
    vision_sync_host_set_http_sequence(statuses, bodies, 3);
    assert(vision_sync_now() == ESP_OK);
    assert(
        std::strstr(
            vision_sync_host_request_body(2),
            "\"failure_code\":\"invalid_frame\""
        ) != nullptr
    );
}

void test_acknowledge_failure_stops_batch_and_keeps_task_open() {
    reset_all();
    const vision_sync_provider_t provider = {
        .analyze = successful_provider,
        .context = nullptr,
    };
    assert(vision_sync_register_provider(&provider) == ESP_OK);
    const int statuses[] = {200, 500};
    const char *bodies[] = {pending_body_with_task(), "{}"};
    vision_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(vision_sync_now() == ESP_FAIL);
    assert(vision_sync_host_request_count() == 2);
    assert(vision_sync_host_camera_accept_calls() == 0);
}

void test_unauthorized_clears_session() {
    reset_all();
    set_single_response(401, "{}");
    assert(vision_sync_now() == ESP_ERR_INVALID_STATE);
    assert(vision_sync_host_clear_session_calls() == 1);

    reset_all();
    set_single_response(403, "{}");
    assert(vision_sync_now() == ESP_ERR_INVALID_STATE);
    assert(vision_sync_host_clear_session_calls() == 1);
}

void test_transport_header_and_post_failures_are_clean() {
    reset_all();
    vision_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(vision_sync_now() == ESP_ERR_INVALID_STATE);
    assert(vision_sync_host_request_count() == 1);

    reset_all();
    vision_sync_host_set_client_init_fails(true);
    assert(vision_sync_now() == ESP_ERR_NO_MEM);
    assert(vision_sync_host_request_count() == 0);

    reset_all();
    vision_sync_host_set_header_failure("Authorization", ESP_ERR_INVALID_ARG);
    assert(vision_sync_now() == ESP_ERR_INVALID_STATE);
    assert(vision_sync_host_request_count() == 1);

    reset_all();
    const vision_sync_provider_t provider = {
        .analyze = successful_provider,
        .context = nullptr,
    };
    assert(vision_sync_register_provider(&provider) == ESP_OK);
    set_closed_loop_responses(pending_body_with_task());
    vision_sync_host_set_post_field_failure(ESP_ERR_INVALID_ARG);
    assert(vision_sync_now() == ESP_ERR_INVALID_STATE);
    // The post-field failure occurs on the result report after the provider
    // ran. Vision lifecycle rejection itself is covered by the acknowledge
    // failure test above.
    assert(vision_sync_host_request_count() == 3);
    assert(vision_sync_host_camera_accept_calls() == 1);
}

void test_invalid_tasks_are_rejected_before_side_effects() {
    reset_all();
    set_single_response(200, pending_body_with_task("", "object_recognition"));
    assert(vision_sync_now() == ESP_ERR_INVALID_ARG);
    assert(vision_sync_host_request_count() == 1);

    reset_all();
    set_single_response(
        200,
        pending_body_with_task(kTaskId, "capture")
    );
    assert(vision_sync_now() == ESP_ERR_INVALID_ARG);

    reset_all();
    set_single_response(
        200,
        pending_body_with_task(kTaskId, "object_recognition", "completed")
    );
    assert(vision_sync_now() == ESP_ERR_INVALID_ARG);

    reset_all();
    static char long_prompt[700];
    std::memset(long_prompt, 'a', sizeof(long_prompt) - 1);
    long_prompt[sizeof(long_prompt) - 1] = '\0';
    set_single_response(200, pending_body_with_task(
        kTaskId,
        "object_recognition",
        "pending",
        long_prompt
    ));
    assert(vision_sync_now() == ESP_ERR_INVALID_ARG);
    assert(vision_sync_host_camera_accept_calls() == 0);
}

void test_duplicate_tasks_are_rejected() {
    reset_all();
    set_single_response(
        200,
        "{\"data\":{\"tasks\":["
        "{\"task_id\":\"same\",\"task_type\":\"object_recognition\","
        "\"status\":\"pending\",\"prompt\":\"\"},"
        "{\"task_id\":\"same\",\"task_type\":\"photo_qa\","
        "\"status\":\"pending\",\"prompt\":\"\"}"
        "]}}"
    );
    assert(vision_sync_now() == ESP_ERR_INVALID_ARG);
    assert(vision_sync_host_camera_accept_calls() == 0);
}

void test_task_batch_limit_preserves_remaining_tasks_for_next_poll() {
    reset_all();
    const vision_sync_provider_t provider = {
        .analyze = successful_provider,
        .context = nullptr,
    };
    assert(vision_sync_register_provider(&provider) == ESP_OK);

    static char pending[
        2048
    ];
    snprintf(
        pending,
        sizeof(pending),
        "{\"data\":{\"tasks\":["
        "{\"task_id\":\"vision-task-0001\",\"task_type\":\"object_recognition\","
        "\"status\":\"pending\",\"prompt\":\"what is this\"},"
        "{\"task_id\":\"vision-task-0002\",\"task_type\":\"object_recognition\","
        "\"status\":\"pending\",\"prompt\":\"what is this\"},"
        "{\"task_id\":\"vision-task-0003\",\"task_type\":\"object_recognition\","
        "\"status\":\"pending\",\"prompt\":\"what is this\"}"
        "]}}"
    );
    const int statuses[] = {200, 200, 201};
    const char *bodies[] = {pending, "{}", "{}"};
    vision_sync_host_set_http_sequence(statuses, bodies, 3);

    // The default test configuration caps one cycle at two tasks. The third
    // task remains pending on the platform and must not turn the response into
    // a failure that would starve the queue.
    assert(vision_sync_now() == ESP_OK);
    assert(vision_sync_host_request_count() == 5);
    assert(
        std::strcmp(
            vision_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "vision/tasks/vision-task-0001/acknowledge"
        ) == 0
    );
    assert(
        std::strcmp(
            vision_sync_host_request_url(3),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "vision/tasks/vision-task-0002/acknowledge"
        ) == 0
    );
}

void test_malformed_queue_response_is_rejected() {
    reset_all();
    set_single_response(200, "not-json");
    assert(vision_sync_now() == ESP_ERR_INVALID_ARG);

    reset_all();
    set_single_response(200, "{\"data\":{}}");
    assert(vision_sync_now() == ESP_ERR_INVALID_ARG);

    reset_all();
    set_single_response(500, "{}");
    assert(vision_sync_now() == ESP_FAIL);
}

void test_initialize_requires_camera_and_is_idempotent() {
    reset_all();
    const module_descriptor_t *const descriptor =
        vision_sync_module_descriptor();
    vision_sync_host_set_camera_ready(false);
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!vision_sync_host_is_ready());
    assert(vision_sync_host_task_create_calls() == 0);

    vision_sync_host_set_camera_ready(true);
    vision_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!vision_sync_host_is_ready());

    vision_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(vision_sync_host_is_ready());
    assert(vision_sync_host_task_create_calls() == 2);

    // Ready is process state, so initialize remains idempotent even after the
    // simulator resets the host dependency fixtures.
    vision_sync_host_reset();
    assert(descriptor->initialize() == ESP_OK);
    assert(vision_sync_host_task_create_calls() == 0);
}

void test_worker_retries_short_and_regular_intervals() {
    reset_all();
    const module_descriptor_t *const descriptor =
        vision_sync_module_descriptor();
    vision_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    vision_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(vision_sync_host_task_pending());

    vision_sync_host_run_pending_task();
    assert(vision_sync_host_request_count() == 0);
    assert(vision_sync_host_delay_calls() == 1);
    assert(vision_sync_host_last_delay_ticks() == 15000u);
    assert(vision_sync_host_task_delete_calls() == 1);
    assert(!vision_sync_host_task_pending());

    reset_all();
    set_single_response(200, "{\"data\":{\"tasks\":[]}}");
    vision_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    vision_sync_host_run_pending_task();
    assert(vision_sync_host_request_count() == 1);
    assert(vision_sync_host_delay_calls() == 1);
    assert(vision_sync_host_last_delay_ticks() == 30000u);
    assert(vision_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_camera_runtime();
    test_unusable_runtime_state_does_not_send_requests();
    test_prepare_propagates_identity_and_base_url_failures();
    test_empty_queue_is_success_without_lifecycle_posts();
    test_successful_result_closes_task_loop();
    test_no_provider_reports_unsupported_failure();
    test_consent_failure_reports_stable_code();
    test_provider_failure_and_invalid_result_report_bounded_codes();
    test_acknowledge_failure_stops_batch_and_keeps_task_open();
    test_unauthorized_clears_session();
    test_transport_header_and_post_failures_are_clean();
    test_invalid_tasks_are_rejected_before_side_effects();
    test_duplicate_tasks_are_rejected();
    test_task_batch_limit_preserves_remaining_tasks_for_next_poll();
    test_malformed_queue_response_is_rejected();
    test_initialize_requires_camera_and_is_idempotent();
    test_worker_retries_short_and_regular_intervals();
    return 0;
}
