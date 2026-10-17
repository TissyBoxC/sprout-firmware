#include "english_course_sync.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "english_course.h"
#include "english_course_sync_host.h"
#include "esp_http_client.h"
#include "transport_security.h"

namespace {

void reset_all() {
    english_course_sync_host_reset_module();
    english_course_sync_host_reset();
    english_course_sync_host_reset_store();
    assert(english_course_init() == ESP_OK);
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    english_course_sync_host_set_http_sequence(statuses, bodies, 1);
}

void seed_attempt(
    const char *lesson_id,
    const char *item_id,
    int64_t practiced_at,
    const char *target_text
) {
    english_course_attempt_t attempt = {};
    strncpy(attempt.lesson_id, lesson_id, ENGLISH_COURSE_ID_SIZE - 1);
    strncpy(attempt.item_id, item_id, ENGLISH_COURSE_ITEM_ID_SIZE - 1);
    attempt.item_type = ENGLISH_ITEM_WORD;
    strncpy(
        attempt.target_text,
        target_text,
        ENGLISH_COURSE_LONG_TEXT_SIZE - 1
    );
    strncpy(
        attempt.recognized_text,
        target_text,
        ENGLISH_COURSE_RECOGNIZED_TEXT_SIZE - 1
    );
    attempt.score = 87;
    attempt.practiced_at_epoch_seconds = practiced_at;
    assert(english_course_record_attempt(&attempt) == ESP_OK);
}

const char *lesson_snapshot =
    "{\"data\":{\"lessons\":[{"
    "\"lesson_id\":\"lesson_fruit\","
    "\"title\":\"Fruit\","
    "\"age_tier\":\"age_3_4\","
    "\"words\":[{"
    "\"id\":\"word_apple\","
    "\"term\":\"apple\","
    "\"phonetic\":\"/apple/\","
    "\"translation\":\"苹果\","
    "\"example\":\"An apple\""
    "}],"
    "\"sentences\":[{"
    "\"id\":\"sentence_apple\","
    "\"pattern\":\"I like apples\","
    "\"prompt\":\"Say it\","
    "\"model_answer\":\"I like apples\""
    "}]}]}}";

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        english_course_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "english_course_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_initialized_course_store() {
    english_course_sync_host_reset();
    english_course_sync_host_reset_module();
    assert(english_course_sync_now() == ESP_ERR_INVALID_STATE);
    assert(english_course_sync_host_request_count() == 0);
}

void test_now_requires_trusted_runtime_state() {
    reset_all();
    english_course_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(english_course_sync_now() == ESP_ERR_INVALID_STATE);

    english_course_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_CONNECTED
    );
    english_course_sync_host_set_time_synchronized(false);
    assert(english_course_sync_now() == ESP_ERR_INVALID_STATE);

    english_course_sync_host_set_time_synchronized(true);
    english_course_sync_host_set_registered(false);
    assert(english_course_sync_now() == ESP_ERR_INVALID_STATE);
    assert(english_course_sync_host_request_count() == 0);
}

void test_now_rejects_unusable_identity_and_base_url() {
    reset_all();
    english_course_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(english_course_sync_now() == ESP_ERR_INVALID_STATE);
    assert(english_course_sync_host_request_count() == 0);

    reset_all();
    english_course_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(english_course_sync_now() == ESP_ERR_INVALID_STATE);
    assert(english_course_sync_host_request_count() == 0);

    reset_all();
    english_course_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(english_course_sync_now() == ESP_ERR_INVALID_STATE);
    assert(english_course_sync_host_request_count() == 0);
}

void test_successful_fetch_replaces_catalog_and_keeps_attempts() {
    reset_all();
    seed_attempt("lesson_old", "word_old", 1700000000, "old");
    set_single_response(200, lesson_snapshot);

    assert(english_course_sync_now() == ESP_OK);
    assert(english_course_sync_host_request_count() == 2);
    assert(
        strcmp(
            english_course_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "english/lessons"
        ) == 0
    );
    assert(
        strcmp(
            english_course_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        english_course_sync_host_request_method(0) == HTTP_METHOD_GET
    );
    assert(english_course_lesson_count() == 1);
    english_course_lesson_t lesson = {};
    assert(english_course_get_lesson("lesson_fruit", &lesson));
    assert(strcmp(lesson.title, "Fruit") == 0);
    assert(lesson.word_count == 1);
    assert(strcmp(lesson.words[0].item_id, "word_apple") == 0);
    assert(strcmp(lesson.words[0].term, "apple") == 0);
    assert(lesson.sentence_count == 1);
    assert(strcmp(lesson.sentences[0].item_id, "sentence_apple") == 0);
    // The catalog refresh must not erase the local practice log.
    assert(english_course_attempt_count() == 1);
}

void test_empty_snapshot_clears_catalog() {
    reset_all();
    const int statuses[] = {200, 201};
    const char *bodies[] = {
        "{\"data\":{\"lessons\":[]}}",
        "{}",
    };
    english_course_sync_host_set_http_sequence(statuses, bodies, 2);
    seed_attempt("lesson_old", "word_old", 1700000000, "old");

    assert(english_course_sync_now() == ESP_OK);
    assert(english_course_lesson_count() == 0);
    assert(english_course_attempt_count() == 1);
    assert(english_course_pending_attempt_count() == 0);
}

void test_malformed_or_rejected_snapshot_keeps_catalog() {
    reset_all();
    set_single_response(200, lesson_snapshot);
    assert(english_course_sync_now() == ESP_OK);
    assert(english_course_lesson_count() == 1);

    set_single_response(200, "{not-json");
    assert(english_course_sync_now() == ESP_ERR_INVALID_ARG);
    assert(english_course_lesson_count() == 1);

    set_single_response(
        200,
        "{\"data\":{\"lessons\":[{\"lesson_id\":\"\"}]}}"
    );
    assert(english_course_sync_now() == ESP_ERR_INVALID_ARG);
    assert(english_course_lesson_count() == 1);
}

void test_unauthorized_fetch_clears_session() {
    reset_all();
    set_single_response(401, "{}");
    assert(english_course_sync_now() == ESP_ERR_INVALID_STATE);
    assert(english_course_sync_host_clear_session_calls() == 1);

    reset_all();
    set_single_response(403, "{}");
    assert(english_course_sync_now() == ESP_ERR_INVALID_STATE);
    assert(english_course_sync_host_clear_session_calls() == 1);
}

void test_transport_failures_are_propagated() {
    reset_all();
    english_course_sync_host_set_client_init_fails(true);
    assert(english_course_sync_now() == ESP_ERR_NO_MEM);

    reset_all();
    english_course_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(english_course_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    english_course_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(english_course_sync_now() == ESP_ERR_INVALID_ARG);
    assert(english_course_sync_host_request_count() == 0);
}

void test_attempts_are_reported_and_acknowledged() {
    reset_all();
    const int statuses[] = {200, 201};
    const char *bodies[] = {lesson_snapshot, "{}"};
    english_course_sync_host_set_http_sequence(statuses, bodies, 2);
    seed_attempt("lesson_fruit", "word_apple", 1700000000, "apple");

    assert(english_course_sync_now() == ESP_OK);
    assert(english_course_sync_host_request_count() == 2);
    assert(
        english_course_sync_host_request_method(1) == HTTP_METHOD_POST
    );
    assert(
        strcmp(
            english_course_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "english/attempts"
        ) == 0
    );
    assert(
        strcmp(
            english_course_sync_host_request_authorization(1),
            "Bearer session-1"
        ) == 0
    );
    assert(
        strstr(
            english_course_sync_host_request_body(1),
            "\"lesson_id\":\"lesson_fruit\""
        ) != nullptr
    );
    assert(
        strstr(
            english_course_sync_host_request_body(1),
            "\"target_text\":\"apple\""
        ) != nullptr
    );
    assert(english_course_pending_attempt_count() == 0);
}

void test_stale_attempt_is_discarded() {
    reset_all();
    const int statuses[] = {200, 404};
    const char *bodies[] = {lesson_snapshot, ""};
    english_course_sync_host_set_http_sequence(statuses, bodies, 2);
    seed_attempt("lesson_removed", "word_apple", 1700000000, "apple");

    assert(english_course_sync_now() == ESP_OK);
    assert(english_course_pending_attempt_count() == 0);
}

void test_attempt_failure_stops_draining_and_retries() {
    reset_all();
    const int failing_statuses[] = {200, 500};
    const char *failing_bodies[] = {lesson_snapshot, ""};
    english_course_sync_host_set_http_sequence(
        failing_statuses,
        failing_bodies,
        2
    );
    seed_attempt("lesson_fruit", "word_apple", 1700000000, "apple");
    seed_attempt("lesson_fruit", "word_apple", 1700000100, "apple");

    assert(english_course_sync_now() == ESP_FAIL);
    assert(english_course_pending_attempt_count() == 2);

    reset_all();
    const int retry_statuses[] = {200, 201, 201};
    const char *retry_bodies[] = {lesson_snapshot, "{}", "{}"};
    english_course_sync_host_set_http_sequence(
        retry_statuses,
        retry_bodies,
        3
    );
    seed_attempt("lesson_fruit", "word_apple", 1700000000, "apple");
    seed_attempt("lesson_fruit", "word_apple", 1700000100, "apple");

    assert(english_course_sync_now() == ESP_OK);
    assert(english_course_pending_attempt_count() == 0);
}

void test_initialize_requires_course_store_and_is_idempotent() {
    english_course_sync_host_reset();
    english_course_sync_host_reset_module();
    english_course_test_reset_ready();
    const module_descriptor_t *descriptor =
        english_course_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!english_course_sync_host_is_ready());
    assert(english_course_sync_host_task_create_calls() == 0);

    assert(english_course_init() == ESP_OK);
    english_course_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!english_course_sync_host_is_ready());

    english_course_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(english_course_sync_host_is_ready());
    assert(english_course_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(english_course_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_and_stops_cleanly() {
    reset_all();
    const module_descriptor_t *descriptor =
        english_course_sync_module_descriptor();
    english_course_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    english_course_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(english_course_sync_host_task_pending());

    english_course_sync_host_run_pending_task();
    assert(english_course_sync_host_request_count() == 0);
    assert(english_course_sync_host_delay_calls() == 1);
    assert(english_course_sync_host_task_delete_calls() == 1);
    assert(!english_course_sync_host_task_pending());
}

void test_worker_syncs_then_stops_cleanly() {
    reset_all();
    const module_descriptor_t *descriptor =
        english_course_sync_module_descriptor();
    set_single_response(200, lesson_snapshot);
    english_course_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    english_course_sync_host_run_pending_task();
    assert(english_course_sync_host_request_count() == 1);
    assert(english_course_sync_host_delay_calls() == 1);
    assert(english_course_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_initialized_course_store();
    test_now_requires_trusted_runtime_state();
    test_now_rejects_unusable_identity_and_base_url();
    test_successful_fetch_replaces_catalog_and_keeps_attempts();
    test_empty_snapshot_clears_catalog();
    test_malformed_or_rejected_snapshot_keeps_catalog();
    test_unauthorized_fetch_clears_session();
    test_transport_failures_are_propagated();
    test_attempts_are_reported_and_acknowledged();
    test_stale_attempt_is_discarded();
    test_attempt_failure_stops_draining_and_retries();
    test_initialize_requires_course_store_and_is_idempotent();
    test_worker_retries_offline_and_stops_cleanly();
    test_worker_syncs_then_stops_cleanly();
    return 0;
}
