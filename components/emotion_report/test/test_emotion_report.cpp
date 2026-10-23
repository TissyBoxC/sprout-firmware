#include <cassert>
#include <cstring>

#include "emotion_report.h"
#include "emotion_report_host.h"

extern "C" {

// Defined by the component only when EMOTION_REPORT_HOST_TESTS is set.
size_t emotion_report_host_queue_count(void);
const char *emotion_report_host_queue_label(size_t index);
const char *emotion_report_host_queue_child_id(size_t index);
int emotion_report_host_queue_confidence(size_t index);
void emotion_report_host_reset_module(void);
void emotion_report_host_set_consecutive_failures(uint32_t failures);
uint32_t emotion_report_host_consecutive_failures(void);
int64_t emotion_report_host_retry_not_before(void);
void emotion_report_host_set_family_disabled(bool is_disabled);
void emotion_report_host_set_stop(bool stop);
void emotion_report_host_worker_iteration(void);

}

namespace {

const int64_t kNow = 1700000000;
const companionship_emotion_t kEmotions[] = {
    COMPANIONSHIP_EMOTION_NEUTRAL,
    COMPANIONSHIP_EMOTION_HAPPY,
    COMPANIONSHIP_EMOTION_SAD,
    COMPANIONSHIP_EMOTION_ANGRY,
    COMPANIONSHIP_EMOTION_SCARED,
    COMPANIONSHIP_EMOTION_TIRED,
};

void reset_all() {
    // The dependency log is cleared first, then the module, so every scenario
    // starts from an uninitialized runtime with no recorded request.
    emotion_report_host_reset();
    emotion_report_host_reset_module();
}

// Initialize the module through its descriptor, as the composition root does.
void initialize() {
    const module_descriptor_t *descriptor =
        emotion_report_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "emotion_report") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->initialize() == ESP_OK);
}

// Queue one sample of a distinct emotion so the duplicate filter never hides a
// sample the test expects to be queued.
void queue(companionship_emotion_t emotion, int64_t observed_at) {
    assert(emotion_report_observe(emotion, observed_at) == ESP_OK);
}

void test_uninitialized_observe_is_refused() {
    reset_all();
    assert(
        emotion_report_observe(COMPANIONSHIP_EMOTION_HAPPY, kNow) ==
        ESP_ERR_INVALID_STATE
    );
    assert(!emotion_report_is_due());
    assert(emotion_report_last_upload() == EMOTION_REPORT_UPLOAD_IDLE);
}

void test_invalid_samples_are_not_queued() {
    reset_all();
    initialize();
    assert(
        emotion_report_observe(
            static_cast<companionship_emotion_t>(99),
            kNow
        ) == ESP_ERR_INVALID_ARG
    );
    assert(
        emotion_report_observe(COMPANIONSHIP_EMOTION_HAPPY, 0) ==
        ESP_ERR_INVALID_STATE
    );
    assert(emotion_report_host_queue_count() == 0);
    assert(!emotion_report_is_due());
}

void test_unregistered_device_does_not_queue() {
    reset_all();
    initialize();
    emotion_report_host_set_registered(false);
    assert(
        emotion_report_observe(COMPANIONSHIP_EMOTION_HAPPY, kNow) ==
        ESP_ERR_INVALID_STATE
    );
    assert(emotion_report_host_queue_count() == 0);
}

void test_observe_only_queues_and_never_calls_out() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_HAPPY, kNow);
    // The caller may be the conversation task: queuing must not block on a
    // request, and the child id is never taken from the device identity.
    assert(emotion_report_host_request_count() == 0);
    assert(emotion_report_host_queue_count() == 1);
    assert(std::strcmp(emotion_report_host_queue_label(0), "happy") == 0);
    assert(emotion_report_host_queue_child_id(0)[0] == '\0');
    assert(emotion_report_host_queue_confidence(0) == 60);
    assert(emotion_report_is_due());
}

void test_duplicate_emotion_within_window_is_coalesced() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_HAPPY, kNow);
    // The same emotion a few seconds later is the same observation for a
    // distribution; a different one is not.
    queue(COMPANIONSHIP_EMOTION_HAPPY, kNow + 2);
    assert(emotion_report_host_queue_count() == 1);
    queue(COMPANIONSHIP_EMOTION_TIRED, kNow + 2);
    assert(emotion_report_host_queue_count() == 2);
    // Outside the window the same emotion is a new observation again.
    queue(COMPANIONSHIP_EMOTION_TIRED, kNow + 60);
    assert(emotion_report_host_queue_count() == 3);
}

void test_queue_is_bounded_and_drops_the_oldest() {
    reset_all();
    initialize();
    for (size_t index = 0; index < 16; ++index) {
        // Distinct instants keep the duplicate filter out of the way.
        queue(
            kEmotions[index % 6],
            kNow - (int64_t)(16 - index) * 60
        );
    }
    assert(emotion_report_host_queue_count() <= 32);
    assert(emotion_report_host_queue_count() == 8);
    // The most recent sample survived and the oldest was evicted.
    assert(std::strcmp(emotion_report_host_queue_label(7), "tired") != 0);
}

void test_successful_upload_sends_the_contract_payload() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_ANGRY, kNow);
    emotion_report_host_set_response_status(201);
    assert(emotion_report_flush() == ESP_OK);
    assert(emotion_report_host_request_count() == 1);
    assert(
        std::strcmp(
            emotion_report_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "emotion-observations"
        ) == 0
    );
    assert(
        emotion_report_host_request_method(0) == HTTP_METHOD_POST
    );
    assert(
        std::strcmp(
            emotion_report_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        std::strcmp(
            emotion_report_host_request_body(0),
            "{\"label\":\"angry\",\"confidence\":60,"
            "\"observed_at\":\"2023-11-14T22:13:20Z\"}"
        ) == 0
    );
    assert(emotion_report_last_upload() == EMOTION_REPORT_UPLOAD_OK);
    assert(emotion_report_host_queue_count() == 0);
    // A device identity is not a child identity, so no child_id is invented.
    assert(
        std::strstr(emotion_report_host_request_body(0), "child_id") ==
        nullptr
    );
}

void test_interval_limits_one_upload_per_minute() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_HAPPY, kNow);
    emotion_report_host_set_response_status(201);
    assert(emotion_report_flush() == ESP_OK);
    // 30 seconds later a new turn is queued, but the gate is still closed, so
    // a caller-driven flush issues no request at all.
    emotion_report_host_set_now_epoch_seconds(kNow + 30);
    queue(COMPANIONSHIP_EMOTION_SAD, kNow + 30);
    assert(emotion_report_flush() == ESP_OK);
    assert(emotion_report_host_request_count() == 1);
    assert(emotion_report_host_queue_count() == 1);
    emotion_report_host_set_now_epoch_seconds(kNow + 61);
    assert(emotion_report_flush() == ESP_OK);
    assert(emotion_report_host_request_count() == 2);
    assert(emotion_report_host_queue_count() == 0);
}

void test_offline_keeps_the_sample_and_backs_off() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SAD, kNow);
    emotion_report_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(emotion_report_host_request_count() == 0);
    assert(emotion_report_flush() == ESP_ERR_INVALID_STATE);
    // The request was never prepared, so nothing counts as a failure and the
    // sample is still waiting for the link to come back.
    assert(emotion_report_host_consecutive_failures() == 0);
    assert(emotion_report_host_queue_count() == 1);

    emotion_report_host_set_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    emotion_report_host_set_response_status(503);
    assert(emotion_report_flush() == ESP_FAIL);
    assert(emotion_report_host_consecutive_failures() == 1);
    assert(emotion_report_host_queue_count() == 1);
    // The backoff keeps the next attempt out until the retry interval passed.
    const int64_t retry_at = emotion_report_host_retry_not_before();
    assert(retry_at == kNow + 30);
    emotion_report_host_set_now_epoch_seconds(kNow + 29);
    assert(emotion_report_flush() == ESP_OK);
    assert(emotion_report_host_request_count() == 1);
    emotion_report_host_set_now_epoch_seconds(retry_at);
    assert(emotion_report_flush() == ESP_FAIL);
    assert(emotion_report_host_consecutive_failures() == 2);
    // The delay doubles per consecutive failure.
    assert(emotion_report_host_retry_not_before() == retry_at + 60);
}

void test_backoff_is_capped() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SAD, kNow);
    emotion_report_host_set_consecutive_failures(20);
    emotion_report_host_set_response_status(500);
    assert(emotion_report_flush() == ESP_FAIL);
    assert(
        emotion_report_host_retry_not_before() ==
        kNow + 900
    );
}

void test_transport_failure_is_not_an_http_status() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SAD, kNow);
    emotion_report_host_set_perform_result(ESP_FAIL);
    assert(emotion_report_flush() == ESP_FAIL);
    assert(emotion_report_last_upload() == EMOTION_REPORT_UPLOAD_RETRY);
    assert(emotion_report_host_queue_count() == 1);
    assert(emotion_report_host_clear_session_calls() == 0);
}

void test_client_allocation_failure_keeps_the_sample() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SAD, kNow);
    emotion_report_host_set_client_init_fails(true);
    assert(emotion_report_flush() == ESP_FAIL);
    assert(emotion_report_host_queue_count() == 1);
}

void test_rejected_sample_is_dropped_and_does_not_wedge_the_queue() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SAD, kNow);
    queue(COMPANIONSHIP_EMOTION_HAPPY, kNow + 20);
    emotion_report_host_set_response_status(422);
    assert(emotion_report_flush() == ESP_OK);
    assert(emotion_report_last_upload() == EMOTION_REPORT_UPLOAD_REJECTED);
    assert(emotion_report_host_queue_count() == 1);
    assert(std::strcmp(emotion_report_host_queue_label(0), "happy") == 0);
    // A permanent rejection is not a link failure, so it must not open a
    // backoff that delays the next, possibly valid, sample.
    assert(emotion_report_host_consecutive_failures() == 0);
    assert(emotion_report_host_retry_not_before() == 0);
}

void test_unauthorized_clears_the_token_and_keeps_the_sample() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SCARED, kNow);
    emotion_report_host_set_response_status(401);
    assert(emotion_report_flush() == ESP_FAIL);
    assert(emotion_report_host_clear_session_calls() == 1);
    assert(emotion_report_host_queue_count() == 1);
    assert(emotion_report_last_upload() == EMOTION_REPORT_UPLOAD_RETRY);
}

void test_family_disabled_stops_queuing_until_reset() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SAD, kNow);
    emotion_report_host_set_response_status(403);
    assert(emotion_report_flush() == ESP_FAIL);
    assert(emotion_report_host_clear_session_calls() == 1);
    // 403 is the family switch being off. The sample waits, but a new turn
    // while the toggle is off must not keep filling the queue.
    emotion_report_host_set_family_disabled(true);
    assert(
        emotion_report_observe(COMPANIONSHIP_EMOTION_HAPPY, kNow + 20) ==
        ESP_ERR_INVALID_STATE
    );
    assert(emotion_report_host_queue_count() == 1);
    assert(!emotion_report_is_due());
    assert(emotion_report_flush() == ESP_OK);
    assert(emotion_report_host_request_count() == 1);
}

void test_missing_session_defers_without_counting_a_failure() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SAD, kNow);
    emotion_report_host_set_copy_session_result(ESP_ERR_INVALID_STATE);
    assert(emotion_report_flush() == ESP_ERR_INVALID_STATE);
    assert(emotion_report_host_request_count() == 0);
    assert(emotion_report_host_consecutive_failures() == 0);
    assert(emotion_report_host_queue_count() == 1);
}

void test_untrusted_clock_defers() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SAD, kNow);
    emotion_report_host_set_time_synchronized(false);
    assert(emotion_report_flush() == ESP_ERR_INVALID_STATE);
    assert(emotion_report_host_request_count() == 0);
    assert(emotion_report_host_queue_count() == 1);
}

void test_insecure_base_url_sends_nothing() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SAD, kNow);
    emotion_report_host_set_base_url("http://api.clarkhub.cn");
    assert(emotion_report_flush() == ESP_ERR_INVALID_ARG);
    assert(emotion_report_host_request_count() == 0);
    assert(emotion_report_host_queue_count() == 1);
}

void test_transport_policy_rejection_drops_the_sample() {
    reset_all();
    initialize();
    queue(COMPANIONSHIP_EMOTION_SAD, kNow);
    emotion_report_host_set_transport_security_result(ESP_ERR_INVALID_ARG);
    assert(emotion_report_flush() == ESP_OK);
    assert(emotion_report_host_request_count() == 0);
    assert(emotion_report_last_upload() == EMOTION_REPORT_UPLOAD_REJECTED);
    assert(emotion_report_host_queue_count() == 0);
}

void test_worker_iteration_uploads_and_clears() {
    reset_all();
    initialize();
    assert(emotion_report_host_task_create_calls() == 1);
    assert(emotion_report_host_task_pending());
    queue(COMPANIONSHIP_EMOTION_HAPPY, kNow);
    emotion_report_host_set_response_status(201);
    // The iteration hook consumes the pending task exactly once. Stopping is
    // what the loop does at its end, so the hook is what makes one pass run;
    // requesting a stop first would skip the body entirely.
    emotion_report_host_worker_iteration();
    assert(emotion_report_host_request_count() == 1);
    assert(emotion_report_host_queue_count() == 0);
    assert(emotion_report_host_delay_calls() == 1);
}

void test_initialize_failure_is_reported() {
    reset_all();
    emotion_report_host_set_task_create_fails(true);
    const module_descriptor_t *descriptor =
        emotion_report_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!emotion_report_is_due());
    assert(
        emotion_report_observe(COMPANIONSHIP_EMOTION_HAPPY, kNow) ==
        ESP_ERR_INVALID_STATE
    );
}

}  // namespace

int main() {
    test_uninitialized_observe_is_refused();
    test_invalid_samples_are_not_queued();
    test_unregistered_device_does_not_queue();
    test_observe_only_queues_and_never_calls_out();
    test_duplicate_emotion_within_window_is_coalesced();
    test_queue_is_bounded_and_drops_the_oldest();
    test_successful_upload_sends_the_contract_payload();
    test_interval_limits_one_upload_per_minute();
    test_offline_keeps_the_sample_and_backs_off();
    test_backoff_is_capped();
    test_transport_failure_is_not_an_http_status();
    test_client_allocation_failure_keeps_the_sample();
    test_rejected_sample_is_dropped_and_does_not_wedge_the_queue();
    test_unauthorized_clears_the_token_and_keeps_the_sample();
    test_family_disabled_stops_queuing_until_reset();
    test_missing_session_defers_without_counting_a_failure();
    test_untrusted_clock_defers();
    test_insecure_base_url_sends_nothing();
    test_transport_policy_rejection_drops_the_sample();
    test_worker_iteration_uploads_and_clears();
    test_initialize_failure_is_reported();
    return 0;
}
