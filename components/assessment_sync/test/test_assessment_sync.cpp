#include "assessment_sync.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "assessment_sync_host.h"
#include "esp_http_client.h"
#include "transport_security.h"

namespace {

const char *const SESSION_ID = "10f6a4c2-3d1f-4c8a-9b6e-2f4a1c7d8e90";

const char *const ACTIVE_SESSION_BODY =
    "{\"data\":{"
    "\"session_id\":\"10f6a4c2-3d1f-4c8a-9b6e-2f4a1c7d8e90\","
    "\"child_id\":\"5d7b0c1e-1111-4222-8333-944455556666\","
    "\"lesson_id\":\"english_greetings\","
    "\"state\":\"active\","
    "\"difficulty\":2,"
    "\"item_index\":1,"
    "\"correct_count\":1,"
    "\"answered_count\":1,"
    "\"streak\":1,"
    "\"mastery\":100,"
    "\"progress\":10,"
    "\"max_items\":10,"
    "\"started_at\":\"2026-10-23T05:00:00Z\","
    "\"expires_at\":\"2026-10-23T05:10:00Z\","
    "\"finish_reason\":\"\","
    "\"accepts_answers\":true,"
    "\"next_item\":{"
    "\"position\":1,"
    "\"item_id\":\"item-0002\","
    "\"item_type\":\"word\","
    "\"text\":\"banana\","
    "\"difficulty\":2"
    "}}}";

const char *const FINISHED_SESSION_BODY =
    "{\"data\":{"
    "\"session_id\":\"10f6a4c2-3d1f-4c8a-9b6e-2f4a1c7d8e90\","
    "\"state\":\"completed\","
    "\"difficulty\":3,"
    "\"item_index\":10,"
    "\"correct_count\":7,"
    "\"answered_count\":10,"
    "\"mastery\":70,"
    "\"progress\":100,"
    "\"max_items\":10,"
    "\"accepts_answers\":false,"
    "\"finish_reason\":\"item_limit\""
    "}}";

void reset_all() {
    assessment_sync_host_reset_module();
    assessment_sync_host_reset();
    // The worker is only started by the module registry in production, so the
    // test drives the same initialize hook the composition root uses.
    assert(assessment_sync_module_descriptor()->initialize() == ESP_OK);
}

void set_single_response(int status, const char *body) {
    const int statuses[] = {status};
    const char *bodies[] = {body};
    assessment_sync_host_set_http_sequence(statuses, bodies, 1);
}

void set_response_sequence(
    const int *statuses,
    const char *const *bodies,
    size_t count
) {
    assessment_sync_host_set_http_sequence(statuses, bodies, count);
}

assessment_sync_status_snapshot_t status() {
    assessment_sync_status_snapshot_t snapshot = {};
    assert(assessment_sync_get_status(&snapshot) == ESP_OK);
    return snapshot;
}

size_t count_request_path(const char *fragment) {
    size_t matches = 0;
    for (size_t index = 0; index < assessment_sync_host_request_count();
         ++index) {
        if (std::strstr(
                assessment_sync_host_request_url(index),
                fragment
            ) != nullptr) {
            matches += 1;
        }
    }
    return matches;
}

const char *const SESSION_PATH =
    "/api/v1/devices/device-0001/assessment-sessions/";

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor = assessment_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "assessment_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_initialize_is_idempotent_and_reports_task_failure() {
    assessment_sync_host_reset_module();
    assessment_sync_host_reset();
    assessment_sync_host_set_task_create_fails(true);
    assert(
        assessment_sync_module_descriptor()->initialize() == ESP_ERR_NO_MEM
    );
    assert(!assessment_sync_host_is_ready());

    assessment_sync_host_set_task_create_fails(false);
    assert(assessment_sync_module_descriptor()->initialize() == ESP_OK);
    assert(assessment_sync_host_is_ready());
    const int created = assessment_sync_host_task_create_calls();
    assert(assessment_sync_module_descriptor()->initialize() == ESP_OK);
    assert(assessment_sync_host_task_create_calls() == created);
}

void test_refresh_requires_an_initialized_module() {
    assessment_sync_host_reset_module();
    assessment_sync_host_reset();
    assert(
        assessment_sync_refresh_session(SESSION_ID) == ESP_ERR_INVALID_STATE
    );
    assert(assessment_sync_host_request_count() == 0);
    assert(assessment_sync_refresh_session(nullptr) == ESP_ERR_INVALID_ARG);
    assert(assessment_sync_refresh_session("") == ESP_ERR_INVALID_ARG);
}

void test_refresh_without_network_is_offline_and_silent() {
    reset_all();
    assessment_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(
        assessment_sync_refresh_session(SESSION_ID) == ESP_ERR_INVALID_STATE
    );
    // No connection may be opened while offline.
    assert(assessment_sync_host_request_count() == 0);
    assert(status().status == ASSESSMENT_SYNC_STATUS_OFFLINE);
}

void test_refresh_rejects_untrusted_identity_and_base_url() {
    reset_all();
    assessment_sync_host_set_identity_result(ESP_ERR_INVALID_STATE);
    assert(
        assessment_sync_refresh_session(SESSION_ID) == ESP_ERR_INVALID_STATE
    );
    assert(assessment_sync_host_request_count() == 0);

    assessment_sync_host_reset();
    assessment_sync_host_set_base_url("http://api.clarkhub.cn");
    // Plaintext transport is refused before any request is built.
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_ERR_INVALID_ARG);
    assert(assessment_sync_host_request_count() == 0);
}

void test_refresh_fetches_the_session_then_exposes_the_item() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assert(assessment_sync_host_request_count() == 1);
    assert(
        std::strcmp(
            assessment_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "assessment-sessions/10f6a4c2-3d1f-4c8a-9b6e-2f4a1c7d8e90"
        ) == 0
    );
    assert(
        assessment_sync_host_request_method(0) == HTTP_METHOD_GET
    );
    const char *authorization = assessment_sync_host_request_authorization(0);
    assert(std::strncmp(authorization, "Bearer ", 7) == 0);
    assert(std::strcmp(authorization + 7, "session-1") == 0);
    assert(assessment_sync_host_transport_security_calls() == 1);

    const assessment_sync_status_snapshot_t snapshot = status();
    assert(snapshot.status == ASSESSMENT_SYNC_STATUS_IDLE);
    assert(snapshot.has_session);
    assert(std::strcmp(snapshot.state, "active") == 0);
    assert(snapshot.accepts_answers);
    assert(snapshot.difficulty == 2);
    assert(snapshot.answered_count == 1);
    assert(snapshot.correct_count == 1);
    assert(snapshot.mastery == 100);
    assert(snapshot.item.has_item);
    assert(snapshot.item.position == 1);
    assert(std::strcmp(snapshot.item.item_id, "item-0002") == 0);
    assert(std::strcmp(snapshot.item.item_type, "word") == 0);
    assert(std::strcmp(snapshot.item.text, "banana") == 0);
    assert(snapshot.pending_count == 0);
    assert(snapshot.last_success_epoch_seconds > 0);
}

void test_unauthorized_clears_the_session_token() {
    reset_all();
    set_single_response(401, "{}");
    assert(
        assessment_sync_refresh_session(SESSION_ID) == ESP_ERR_INVALID_STATE
    );
    assert(assessment_sync_host_clear_session_calls() == 1);
    assert(status().status == ASSESSMENT_SYNC_STATUS_OFFLINE);
    assert(!status().has_session);
}

void test_unknown_session_does_not_track_it() {
    reset_all();
    set_single_response(404, "{}");
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_ERR_NOT_FOUND);
    // A 404 is permanent for this id: the token is still usable.
    assert(assessment_sync_host_clear_session_calls() == 0);
    assert(status().status == ASSESSMENT_SYNC_STATUS_FAILED);
    assert(!status().has_session);
}

void test_unusable_payload_keeps_the_previous_snapshot() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    const int64_t first_success = status().last_success_epoch_seconds;

    const int statuses[] = {200};
    const char *bodies[] = {"{\"data\":{\"session_id\":\"s\"}}"};
    set_response_sequence(statuses, bodies, 1);
    assert(
        assessment_sync_refresh_session(SESSION_ID) == ESP_ERR_INVALID_ARG
    );
    // The item the child is looking at must not be wiped by a bad response.
    const assessment_sync_status_snapshot_t snapshot = status();
    assert(std::strcmp(snapshot.item.item_id, "item-0002") == 0);
    assert(snapshot.last_success_epoch_seconds == first_success);
}

void test_finished_session_clears_the_item() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    set_single_response(200, FINISHED_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    const assessment_sync_status_snapshot_t snapshot = status();
    assert(std::strcmp(snapshot.state, "completed") == 0);
    assert(!snapshot.accepts_answers);
    assert(!snapshot.item.has_item);
    assert(snapshot.mastery == 70);
}

void test_submit_requires_a_refreshed_session() {
    reset_all();
    assert(
        assessment_sync_submit_answer(
            SESSION_ID,
            "item-0002",
            "word",
            "banana",
            90
        ) == ESP_ERR_INVALID_STATE
    );
}

void test_submit_rejects_a_score_the_guardian_could_not_have_given() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assert(
        assessment_sync_submit_answer(
            SESSION_ID,
            "item-0002",
            "word",
            "banana",
            120
        ) == ESP_ERR_INVALID_ARG
    );
    // The firmware has no speech recognition; a second item cannot be graded
    // while the guardian is looking at the first.
    assert(
        assessment_sync_submit_answer(
            SESSION_ID,
            "item-0009",
            "word",
            "banana",
            80
        ) == ESP_ERR_INVALID_ARG
    );
    assert(
        assessment_sync_submit_answer(
            SESSION_ID,
            "item-0002",
            "word",
            "banana",
            80
        ) == ESP_OK
    );
    assert(status().pending_count == 1);
    // Answering the same item twice is refused instead of consumed twice.
    assert(
        assessment_sync_submit_answer(
            SESSION_ID,
            "item-0002",
            "word",
            "banana",
            90
        ) == ESP_ERR_NO_MEM
    );
}

void test_upload_omits_position_and_drains_the_queue() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assert(
        assessment_sync_submit_answer(
            SESSION_ID,
            "item-0002",
            "word",
            "banana",
            75
        ) == ESP_OK
    );
    // The run uploads first, so request 0 is the answer and request 1 is the
    // refreshed session that carries the next item.
    const int statuses[] = {201, 200};
    const char *bodies[] = {
        "{\"data\":{\"session_id\":\"10f6a4c2-3d1f-4c8a-9b6e-2f4a1c7d8e90\","
        "\"state\":\"active\",\"accepts_answers\":true,\"difficulty\":2,"
        "\"answered_count\":2,\"correct_count\":2,\"mastery\":100,"
        "\"next_item\":{\"position\":2,\"item_id\":\"item-0003\","
        "\"item_type\":\"word\",\"text\":\"cherry\",\"difficulty\":2}}}",
        "{\"data\":{\"session_id\":\"10f6a4c2-3d1f-4c8a-9b6e-2f4a1c7d8e90\","
        "\"state\":\"active\",\"accepts_answers\":true,\"difficulty\":2,"
        "\"answered_count\":2,\"correct_count\":2,\"mastery\":100,"
        "\"next_item\":{\"position\":2,\"item_id\":\"item-0003\","
        "\"item_type\":\"word\",\"text\":\"cherry\",\"difficulty\":2}}}",
    };
    // The stub indexes responses by request number, so the recorder is cleared
    // to make request 0 the first request of this cycle.
    assessment_sync_host_reset();
    assessment_sync_host_set_http_sequence(statuses, bodies, 2);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assert(count_request_path("/answers") == 1);
    assert(
        assessment_sync_host_request_method(0) == HTTP_METHOD_POST
    );
    assert(assessment_sync_host_request_method(1) == HTTP_METHOD_GET);
    const char *body = assessment_sync_host_request_body(0);
    assert(std::strstr(body, "\"item_id\":\"item-0002\"") != nullptr);
    assert(std::strstr(body, "\"item_type\":\"word\"") != nullptr);
    assert(std::strstr(body, "\"target_text\":\"banana\"") != nullptr);
    assert(std::strstr(body, "\"score\":75") != nullptr);
    // The platform derives the next position itself; sending one is rejected
    // as an unknown field by the device handler.
    assert(std::strstr(body, "position") == nullptr);
    assert(status().pending_count == 0);
    assert(std::strcmp(status().item.item_id, "item-0003") == 0);
}

void test_transient_failure_keeps_the_answer_queued() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assert(
        assessment_sync_submit_answer(
            SESSION_ID,
            "item-0002",
            "word",
            "banana",
            40
        ) == ESP_OK
    );
    set_single_response(500, "{}");
    assert(
        assessment_sync_refresh_session(SESSION_ID) == ESP_FAIL
    );
    // The answer survives so a platform outage never loses the guardian's grade.
    assert(status().pending_count == 1);
    assert(status().status == ASSESSMENT_SYNC_STATUS_FAILED);
}

void test_permanently_rejected_answer_is_dropped() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assert(
        assessment_sync_submit_answer(
            SESSION_ID,
            "item-0002",
            "word",
            "banana",
            40
        ) == ESP_OK
    );
    // 422 means the platform will never accept this answer; keeping it queued
    // would block every later answer behind it. Request 0 is the refused POST,
    // request 1 the refresh that follows it.
    const int statuses[] = {422, 200};
    const char *bodies[] = {"{}", ACTIVE_SESSION_BODY};
    assessment_sync_host_reset();
    set_response_sequence(statuses, bodies, 2);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assert(status().pending_count == 0);
}

void test_new_session_discards_the_previous_queue() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assert(
        assessment_sync_submit_answer(
            SESSION_ID,
            "item-0002",
            "word",
            "banana",
            60
        ) == ESP_OK
    );
    // A new session id starts from a clean recorder, so request 0 is the pull
    // for the new run rather than a leftover response for the old one.
    assessment_sync_host_reset();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session("ffffffff-0000-0000-0000-000000000001")
        == ESP_OK);
    // A queued answer belongs to the old run and can never address the new one.
    assert(status().pending_count == 0);
}

void test_reset_clears_the_tracked_session() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assert(assessment_sync_reset() == ESP_OK);
    const assessment_sync_status_snapshot_t snapshot = status();
    assert(!snapshot.has_session);
    assert(!snapshot.item.has_item);
    assert(snapshot.pending_count == 0);
    assert(snapshot.status == ASSESSMENT_SYNC_STATUS_IDLE);
}

void test_worker_backs_off_then_stops_cleanly() {
    reset_all();
    // Track the session first, then make every later cycle fail.
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assessment_sync_host_reset();
    set_single_response(500, "{}");
    // The stub stops the worker after this many delays, so the loop runs
    // delay_count + 1 cycles.
    assessment_sync_host_set_stop_after_delays(3);
    assessment_sync_host_run_pending_task();
    assert(assessment_sync_host_task_delete_calls() == 1);
    assert(!assessment_sync_host_task_pending());
    assert(assessment_sync_host_delay_calls() == 4);
    // Two failures happened before this cycle in the suite, and a successful
    // refresh does not reset the counter, so the fourth failure anywhere in the
    // module's life sleeps 15 * 2^4 = 240 seconds. The doubling itself is
    // asserted exactly, and cheaply, in assessment_sync_core.
    assert(assessment_sync_host_last_delay_seconds() == 240);
}

void test_worker_polls_a_healthy_session_at_the_normal_interval() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    assessment_sync_host_reset();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assessment_sync_host_set_stop_after_delays(1);
    assessment_sync_host_run_pending_task();
    // A successful cycle falls back to the poll interval instead of growing.
    assert(assessment_sync_host_last_delay_seconds() == 60);
}

void test_status_carries_no_child_identifier() {
    reset_all();
    set_single_response(200, ACTIVE_SESSION_BODY);
    assert(assessment_sync_refresh_session(SESSION_ID) == ESP_OK);
    const assessment_sync_status_snapshot_t snapshot = status();
    // The snapshot is the only handle a caller or a diagnostic has on the run.
    // It must describe the item, never the child: the component deliberately
    // does not store child_id at all, so no code path can log or forward it.
    const char *child_marker = "5d7b0c1e";
    const char *const fields[] = {
        snapshot.state,
        snapshot.item.item_id,
        snapshot.item.item_type,
        snapshot.item.text,
    };
    for (const char *field : fields) {
        assert(std::strstr(field, child_marker) == nullptr);
    }
    assert(std::strcmp(snapshot.item.text, "banana") == 0);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_initialize_is_idempotent_and_reports_task_failure();
    // Readiness-gated cases run before anything initializes the module,
    // because the ready flag is sticky by contract.
    test_refresh_requires_an_initialized_module();
    test_refresh_without_network_is_offline_and_silent();
    test_refresh_rejects_untrusted_identity_and_base_url();
    test_refresh_fetches_the_session_then_exposes_the_item();
    test_unauthorized_clears_the_session_token();
    test_unknown_session_does_not_track_it();
    test_unusable_payload_keeps_the_previous_snapshot();
    test_finished_session_clears_the_item();
    test_submit_requires_a_refreshed_session();
    test_submit_rejects_a_score_the_guardian_could_not_have_given();
    test_upload_omits_position_and_drains_the_queue();
    test_transient_failure_keeps_the_answer_queued();
    test_permanently_rejected_answer_is_dropped();
    test_new_session_discards_the_previous_queue();
    test_reset_clears_the_tracked_session();
    test_worker_backs_off_then_stops_cleanly();
    test_worker_polls_a_healthy_session_at_the_normal_interval();
    test_status_carries_no_child_identifier();
    std::printf("assessment_sync_runtime: all checks passed\n");
    return 0;
}
