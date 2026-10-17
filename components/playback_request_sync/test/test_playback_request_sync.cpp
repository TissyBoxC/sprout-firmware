#include "playback_request_sync.h"

#include <cassert>
#include <cstring>

#include "esp_http_client.h"
#include "network_manager.h"
#include "playback_request.h"
#include "playback_request_sync_host.h"
#include "transport_security.h"

namespace {

void reset_all() {
    playback_request_test_reset_ready();
    playback_request_sync_host_reset_module();
    playback_request_sync_host_reset();
    playback_request_init();
}

playback_request_t remote_request(
    const char *request_id,
    const char *package_id,
    playback_request_status_t status,
    const char *note,
    int64_t requested_at
) {
    playback_request_t request = {};
    strncpy(request.request_id, request_id, sizeof(request.request_id) - 1);
    strncpy(request.package_id, package_id, sizeof(request.package_id) - 1);
    request.status = status;
    request.outcome = PLAYBACK_REQUEST_OUTCOME_NONE;
    strncpy(request.note, note, sizeof(request.note) - 1);
    request.requested_at_epoch_seconds = requested_at;
    return request;
}

void add_local_request(
    const char *request_id,
    const char *package_id,
    playback_request_status_t status
) {
    const playback_request_t request = remote_request(
        request_id,
        package_id,
        status,
        "",
        1000
    );
    assert(playback_request_upsert(&request) == ESP_OK);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *const descriptor =
        playback_request_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "playback_request_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_ready_cache() {
    playback_request_test_reset_ready();
    playback_request_sync_host_reset_module();
    playback_request_sync_host_reset();
    assert(playback_request_sync_now() == ESP_ERR_INVALID_STATE);
    assert(playback_request_sync_host_request_count() == 0);
}

void test_fetch_rejects_unusable_identity_and_base_url() {
    reset_all();
    playback_request_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(playback_request_sync_now() == ESP_ERR_INVALID_SIZE);
    assert(playback_request_sync_host_request_count() == 0);

    reset_all();
    playback_request_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(playback_request_sync_now() == ESP_ERR_INVALID_ARG);
    assert(playback_request_sync_host_request_count() == 0);

    reset_all();
    playback_request_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(playback_request_sync_now() == ESP_ERR_INVALID_STATE);
    assert(playback_request_sync_host_request_count() == 0);
}

void test_successful_fetch_merges_remote_and_applies_actions() {
    reset_all();
    add_local_request(
        "req-local-pending",
        "pkg-local",
        PLAYBACK_REQUEST_PENDING
    );

    const char *const fetch_body =
        "{\"data\":{\"requests\":["
        "{\"id\":\"req-remote\",\"package_id\":\"pkg-remote\","
        "\"note\":\"play this\",\"status\":\"pending\","
        "\"requested_at\":\"2026-10-17T12:00:00Z\"}"
        "]}}";
    const int statuses[] = {200, 200};
    const char *bodies[] = {fetch_body, "{}"};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(playback_request_sync_now() == ESP_OK);
    assert(playback_request_sync_host_request_count() == 1);
    assert(
        strcmp(
            playback_request_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "playback/requests"
        ) == 0
    );
    assert(
        strcmp(
            playback_request_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        playback_request_sync_host_request_method(0) == HTTP_METHOD_GET
    );
    assert(playback_request_count() == 1);
    assert(playback_request_get("req-local-pending", nullptr) == false);

    playback_request_t merged = {};
    assert(playback_request_get("req-remote", &merged));
    assert(strcmp(merged.package_id, "pkg-remote") == 0);
    assert(merged.status == PLAYBACK_REQUEST_PENDING);
    // 2026-10-17T12:00:00Z is parsed into epoch seconds, not left at zero.
    assert(merged.requested_at_epoch_seconds == 1792238400);
}

void test_requested_at_accepts_fractional_seconds() {
    reset_all();
    const char *const fetch_body =
        "{\"data\":{\"requests\":["
        "{\"id\":\"req-frac\",\"package_id\":\"pkg-frac\","
        "\"status\":\"pending\","
        "\"requested_at\":\"2026-10-17T12:00:00.250Z\"}"
        "]}}";
    const int statuses[] = {200};
    const char *bodies[] = {fetch_body};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 1);

    assert(playback_request_sync_now() == ESP_OK);
    playback_request_t merged = {};
    assert(playback_request_get("req-frac", &merged));
    assert(merged.requested_at_epoch_seconds == 1792238400);
}

void test_local_terminal_history_survives_remote_merge() {
    reset_all();
    add_local_request(
        "req-finished",
        "pkg-finished",
        PLAYBACK_REQUEST_COMPLETED
    );

    const char *const fetch_body =
        "{\"data\":{\"requests\":["
        "{\"id\":\"req-finished\",\"package_id\":\"pkg-finished\","
        "\"status\":\"pending\",\"requested_at\":\"2026-10-17T12:00:00Z\"}"
        "]}}";
    const int statuses[] = {200};
    const char *bodies[] = {fetch_body};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 1);

    assert(playback_request_sync_now() == ESP_OK);
    playback_request_t request = {};
    assert(playback_request_get("req-finished", &request));
    assert(request.status == PLAYBACK_REQUEST_COMPLETED);
}

void test_local_acknowledge_and_complete_drain_in_order() {
    reset_all();
    add_local_request(
        "req-lifecycle",
        "pkg-lifecycle",
        PLAYBACK_REQUEST_PENDING
    );
    assert(playback_request_acknowledge("req-lifecycle") == ESP_OK);
    assert(
        playback_request_complete(
            "req-lifecycle",
            PLAYBACK_REQUEST_OUTCOME_PLAYED
        ) == ESP_OK
    );
    assert(playback_request_pending_outbound_count() == 1);

    const int statuses[] = {200, 200};
    const char *bodies[] = {"{\"data\":{\"requests\":[]}}", "{}"};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(playback_request_sync_now() == ESP_OK);
    assert(playback_request_sync_host_request_count() == 2);
    assert(playback_request_pending_outbound_count() == 0);
    assert(
        strcmp(
            playback_request_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "playback/requests/req-lifecycle/complete"
        ) == 0
    );
    assert(
        playback_request_sync_host_request_method(1) == HTTP_METHOD_POST
    );
    assert(
        strcmp(
            playback_request_sync_host_request_body(1),
            "{\"outcome\":\"played\"}"
        ) == 0
    );
}

void test_acknowledge_action_has_no_completion_body() {
    reset_all();
    add_local_request(
        "req-ack",
        "pkg-ack",
        PLAYBACK_REQUEST_PENDING
    );
    assert(playback_request_acknowledge("req-ack") == ESP_OK);

    const int statuses[] = {200, 200};
    const char *bodies[] = {"{\"data\":{\"requests\":[]}}", "{}"};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(playback_request_sync_now() == ESP_OK);
    assert(
        strcmp(
            playback_request_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "playback/requests/req-ack/acknowledge"
        ) == 0
    );
    assert(strcmp(playback_request_sync_host_request_body(1), "") == 0);
}

void test_removed_request_action_is_discarded_and_next_continues() {
    reset_all();
    add_local_request("req-gone", "pkg-gone", PLAYBACK_REQUEST_PENDING);
    add_local_request("req-live", "pkg-live", PLAYBACK_REQUEST_PENDING);
    assert(playback_request_acknowledge("req-gone") == ESP_OK);
    assert(playback_request_acknowledge("req-live") == ESP_OK);

    const int statuses[] = {200, 404, 200};
    const char *bodies[] = {
        "{\"data\":{\"requests\":[]}}",
        "{}",
        "{}",
    };
    playback_request_sync_host_set_http_sequence(statuses, bodies, 3);

    assert(playback_request_sync_now() == ESP_OK);
    assert(playback_request_sync_host_request_count() == 3);
    assert(playback_request_pending_outbound_count() == 0);
}

void test_transient_action_failure_stops_and_keeps_remaining() {
    reset_all();
    add_local_request("req-one", "pkg-one", PLAYBACK_REQUEST_PENDING);
    add_local_request("req-two", "pkg-two", PLAYBACK_REQUEST_PENDING);
    assert(playback_request_acknowledge("req-one") == ESP_OK);
    assert(playback_request_acknowledge("req-two") == ESP_OK);

    const int statuses[] = {200, 200, 500};
    const char *bodies[] = {
        "{\"data\":{\"requests\":["
        "{\"id\":\"req-one\",\"package_id\":\"pkg-one\","
        "\"status\":\"pending\",\"requested_at\":\"2026-10-17T12:00:00Z\"},"
        "{\"id\":\"req-two\",\"package_id\":\"pkg-two\","
        "\"status\":\"pending\",\"requested_at\":\"2026-10-17T12:00:00Z\"}"
        "]}}",
        "{}",
        "{}",
    };
    playback_request_sync_host_set_http_sequence(statuses, bodies, 3);

    assert(playback_request_sync_now() == ESP_OK);
    assert(playback_request_sync_host_request_count() == 3);
    assert(playback_request_pending_outbound_count() == 1);
}

void test_unauthorized_fetch_clears_session() {
    reset_all();
    const int statuses[] = {401};
    const char *bodies[] = {"{}"};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(playback_request_sync_now() == ESP_ERR_INVALID_STATE);
    assert(playback_request_sync_host_clear_session_calls() == 1);
}

void test_malformed_snapshot_is_rejected() {
    reset_all();
    const int statuses[] = {200};
    const char *bodies[] = {"{\"data\":{}}"};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 1);
    assert(playback_request_sync_now() == ESP_ERR_INVALID_ARG);
}

// A single unusable entry rejects the whole snapshot so a local non-terminal
// request is never dropped as if the guardian had cancelled it.
void test_bad_entry_rejects_whole_snapshot() {
    reset_all();
    add_local_request("req-local", "pkg-local", PLAYBACK_REQUEST_PENDING);
    const char *const fetch_body =
        "{\"data\":{\"requests\":["
        "{\"id\":\"req-ok\",\"package_id\":\"pkg-ok\","
        "\"status\":\"pending\","
        "\"requested_at\":\"2026-10-17T12:00:00Z\"},"
        "{\"id\":\"req-bad\",\"package_id\":\"pkg-bad\","
        "\"status\":\"pending\",\"requested_at\":\"not-a-date\"}"
        "]}}";
    const int statuses[] = {200};
    const char *bodies[] = {fetch_body};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 1);

    assert(playback_request_sync_now() == ESP_ERR_INVALID_ARG);
    assert(playback_request_count() == 1);
    playback_request_t local = {};
    assert(playback_request_get("req-local", &local));
    playback_request_t absent = {};
    assert(!playback_request_get("req-ok", &absent));
}

void test_missing_and_non_utc_requested_at_are_rejected() {
    const char *const missing =
        "{\"data\":{\"requests\":["
        "{\"id\":\"req-1\",\"package_id\":\"pkg-1\",\"status\":\"pending\"}"
        "]}}";
    const char *const offset =
        "{\"data\":{\"requests\":["
        "{\"id\":\"req-1\",\"package_id\":\"pkg-1\",\"status\":\"pending\","
        "\"requested_at\":\"2026-10-17T12:00:00+08:00\"}"
        "]}}";

    reset_all();
    {
        const int statuses[] = {200};
        const char *bodies[] = {missing};
        playback_request_sync_host_set_http_sequence(statuses, bodies, 1);
        assert(playback_request_sync_now() == ESP_ERR_INVALID_ARG);
    }

    reset_all();
    {
        const int statuses[] = {200};
        const char *bodies[] = {offset};
        playback_request_sync_host_set_http_sequence(statuses, bodies, 1);
        assert(playback_request_sync_now() == ESP_ERR_INVALID_ARG);
    }
}

void test_transport_failures_are_propagated() {
    reset_all();
    playback_request_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(playback_request_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    playback_request_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_HOST_NOT_ALLOWED
    );
    assert(playback_request_sync_now() == ESP_ERR_INVALID_ARG);
    assert(playback_request_sync_host_request_count() == 0);
    assert(playback_request_sync_host_transport_security_calls() == 1);
}

void test_initialize_requires_cache_and_is_idempotent() {
    playback_request_test_reset_ready();
    playback_request_sync_host_reset_module();
    playback_request_sync_host_reset();
    const module_descriptor_t *const descriptor =
        playback_request_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!playback_request_sync_host_is_ready());
    assert(playback_request_sync_host_task_create_calls() == 0);

    assert(playback_request_init() == ESP_OK);
    playback_request_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!playback_request_sync_host_is_ready());

    playback_request_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(playback_request_sync_host_is_ready());
    assert(playback_request_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(playback_request_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_with_short_interval() {
    reset_all();
    const module_descriptor_t *const descriptor =
        playback_request_sync_module_descriptor();
    playback_request_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    playback_request_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(playback_request_sync_host_task_pending());

    playback_request_sync_host_run_pending_task();
    assert(playback_request_sync_host_request_count() == 0);
    assert(playback_request_sync_host_delay_calls() == 1);
    assert(playback_request_sync_host_last_delay_ticks() == 15000u);
    assert(playback_request_sync_host_task_delete_calls() == 1);
    assert(!playback_request_sync_host_task_pending());
}

void test_worker_syncs_then_uses_regular_interval() {
    reset_all();
    const module_descriptor_t *const descriptor =
        playback_request_sync_module_descriptor();
    const int statuses[] = {200};
    const char *bodies[] = {"{\"data\":{\"requests\":[]}}"};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 1);
    playback_request_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    playback_request_sync_host_run_pending_task();
    assert(playback_request_sync_host_request_count() == 1);
    assert(playback_request_sync_host_delay_calls() == 1);
    assert(playback_request_sync_host_last_delay_ticks() == 60000u);
    assert(playback_request_sync_host_task_delete_calls() == 1);
}

void test_worker_uses_regular_interval_for_server_failure() {
    reset_all();
    const module_descriptor_t *const descriptor =
        playback_request_sync_module_descriptor();
    const int statuses[] = {500};
    const char *bodies[] = {"{}"};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 1);
    playback_request_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    playback_request_sync_host_run_pending_task();
    assert(playback_request_sync_host_request_count() == 1);
    assert(playback_request_sync_host_last_delay_ticks() == 60000u);
    assert(playback_request_sync_host_task_delete_calls() == 1);
}

void test_worker_uses_regular_interval_for_rejected_snapshot() {
    reset_all();
    const module_descriptor_t *const descriptor =
        playback_request_sync_module_descriptor();
    const int statuses[] = {200};
    const char *bodies[] = {"{\"data\":{}}"};
    playback_request_sync_host_set_http_sequence(statuses, bodies, 1);
    playback_request_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    playback_request_sync_host_run_pending_task();
    assert(playback_request_sync_host_request_count() == 1);
    assert(playback_request_sync_host_last_delay_ticks() == 60000u);
    assert(playback_request_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_ready_cache();
    test_fetch_rejects_unusable_identity_and_base_url();
    test_successful_fetch_merges_remote_and_applies_actions();
    test_requested_at_accepts_fractional_seconds();
    test_local_terminal_history_survives_remote_merge();
    test_local_acknowledge_and_complete_drain_in_order();
    test_acknowledge_action_has_no_completion_body();
    test_removed_request_action_is_discarded_and_next_continues();
    test_transient_action_failure_stops_and_keeps_remaining();
    test_unauthorized_fetch_clears_session();
    test_malformed_snapshot_is_rejected();
    test_bad_entry_rejects_whole_snapshot();
    test_missing_and_non_utc_requested_at_are_rejected();
    test_transport_failures_are_propagated();
    test_initialize_requires_cache_and_is_idempotent();
    test_worker_retries_offline_with_short_interval();
    test_worker_syncs_then_uses_regular_interval();
    test_worker_uses_regular_interval_for_server_failure();
    test_worker_uses_regular_interval_for_rejected_snapshot();
    return 0;
}
