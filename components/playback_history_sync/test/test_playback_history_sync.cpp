#include "playback_history_sync.h"

#include <cassert>
#include <cstring>

#include "config_store.h"
#include "esp_http_client.h"
#include "network_manager.h"
#include "playback_history.h"
#include "playback_history_sync_host.h"
#include "transport_security.h"

namespace {

void reset_all() {
    playback_history_test_reset_ready();
    playback_history_sync_host_reset_module();
    playback_history_sync_host_reset();
    playback_history_test_store_reset();
    assert(playback_history_init() == ESP_OK);
}

playback_record_t record(
    const char *package_id,
    uint32_t version,
    playback_outcome_t outcome,
    uint32_t seconds,
    int64_t played_at
) {
    playback_record_t value = {};
    strncpy(value.package_id, package_id, sizeof(value.package_id) - 1);
    value.package_version = version;
    value.outcome = outcome;
    value.played_seconds = seconds;
    value.played_at = played_at;
    return value;
}

void queue_record(
    const char *package_id,
    uint32_t version,
    playback_outcome_t outcome,
    uint32_t seconds,
    int64_t played_at
) {
    const playback_record_t value =
        record(package_id, version, outcome, seconds, played_at);
    assert(playback_history_record(&value, nullptr) == ESP_OK);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *const descriptor =
        playback_history_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "playback_history_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_ready_history_store() {
    playback_history_sync_host_reset_module();
    playback_history_sync_host_reset();
    playback_history_test_store_reset();
    playback_history_test_reset_ready();
    assert(playback_history_sync_now() == ESP_ERR_INVALID_STATE);
    assert(playback_history_sync_host_request_count() == 0);
}

void test_now_requires_trusted_runtime_state() {
    reset_all();
    playback_history_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_request_count() == 0);

    playback_history_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_CONNECTED
    );
    playback_history_sync_host_set_time_synchronized(false);
    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_request_count() == 0);

    playback_history_sync_host_set_time_synchronized(true);
    playback_history_sync_host_set_registered(false);
    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_request_count() == 0);
}

void test_successful_report_drains_queue_in_order() {
    reset_all();
    queue_record("pkg-old", 2, PLAYBACK_OUTCOME_COMPLETED, 90, 1000);
    queue_record("pkg-new", 3, PLAYBACK_OUTCOME_INTERRUPTED, 45, 2000);
    assert(playback_history_pending_count() == 2);

    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_request_count() == 2);
    assert(playback_history_pending_count() == 0);

    assert(
        strcmp(
            playback_history_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/content/playback"
        ) == 0
    );
    assert(
        strcmp(
            playback_history_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        playback_history_sync_host_request_method(0) ==
        HTTP_METHOD_POST
    );
    assert(
        strcmp(
            playback_history_sync_host_request_content_type(0),
            "application/json"
        ) == 0
    );
    assert(
        strcmp(
            playback_history_sync_host_request_body(0),
            "{\"package_id\":\"pkg-old\",\"package_version\":2,"
            "\"outcome\":\"completed\",\"played_seconds\":90}"
        ) == 0
    );
    assert(
        strcmp(
            playback_history_sync_host_request_body(1),
            "{\"package_id\":\"pkg-new\",\"package_version\":3,"
            "\"outcome\":\"interrupted\",\"played_seconds\":45}"
        ) == 0
    );
}

void test_removed_package_is_discarded_and_queue_continues() {
    reset_all();
    queue_record("pkg-gone", 1, PLAYBACK_OUTCOME_COMPLETED, 30, 1000);
    queue_record("pkg-live", 4, PLAYBACK_OUTCOME_COMPLETED, 60, 2000);

    const int statuses[] = {404, 200};
    const char *bodies[] = {"{}", "{}"};
    playback_history_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_request_count() == 2);
    assert(playback_history_pending_count() == 0);
}

void test_transient_failure_stops_and_keeps_remaining_rows() {
    reset_all();
    queue_record("pkg-first", 1, PLAYBACK_OUTCOME_COMPLETED, 10, 1000);
    queue_record("pkg-second", 1, PLAYBACK_OUTCOME_COMPLETED, 20, 2000);
    queue_record("pkg-third", 1, PLAYBACK_OUTCOME_COMPLETED, 30, 3000);

    const int statuses[] = {200, 500};
    const char *bodies[] = {"{}", "{}"};
    playback_history_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_request_count() == 2);
    assert(playback_history_pending_count() == 2);

    playback_record_t pending[PLAYBACK_HISTORY_OUTBOUND_CAPACITY] = {};
    size_t count = 0;
    assert(
        playback_history_list_pending(
            pending,
            PLAYBACK_HISTORY_OUTBOUND_CAPACITY,
            &count
        ) == ESP_OK
    );
    assert(count == 2);
    assert(strcmp(pending[0].package_id, "pkg-second") == 0);
    assert(strcmp(pending[1].package_id, "pkg-third") == 0);
}

void test_unauthorized_and_revoked_clear_session() {
    reset_all();
    queue_record("pkg-401", 1, PLAYBACK_OUTCOME_COMPLETED, 10, 1000);
    playback_history_sync_host_set_http_status(401);
    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_clear_session_calls() == 1);
    assert(playback_history_pending_count() == 1);

    reset_all();
    queue_record("pkg-403", 1, PLAYBACK_OUTCOME_COMPLETED, 10, 1000);
    playback_history_sync_host_set_http_status(403);
    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_clear_session_calls() == 1);
    assert(playback_history_pending_count() == 1);
}

void test_identity_and_transport_failures_keep_rows() {
    reset_all();
    queue_record("pkg-identity", 1, PLAYBACK_OUTCOME_COMPLETED, 10, 1000);
    playback_history_sync_host_set_copy_session_result(
        ESP_ERR_INVALID_STATE
    );
    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_request_count() == 0);
    assert(playback_history_pending_count() == 1);

    reset_all();
    queue_record("pkg-base", 1, PLAYBACK_OUTCOME_COMPLETED, 10, 1000);
    playback_history_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_request_count() == 0);
    assert(playback_history_pending_count() == 1);

    reset_all();
    queue_record("pkg-policy", 1, PLAYBACK_OUTCOME_COMPLETED, 10, 1000);
    playback_history_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_HOST_NOT_ALLOWED
    );
    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_request_count() == 0);
    assert(playback_history_sync_host_transport_security_calls() == 1);
    assert(playback_history_pending_count() == 1);
}

void test_transport_failure_keeps_rows() {
    reset_all();
    queue_record("pkg-timeout", 1, PLAYBACK_OUTCOME_COMPLETED, 10, 1000);
    playback_history_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_sync_host_request_count() == 1);
    assert(playback_history_pending_count() == 1);

    reset_all();
    queue_record("pkg-nomem", 1, PLAYBACK_OUTCOME_COMPLETED, 10, 1000);
    playback_history_sync_host_set_client_init_fails(true);
    assert(playback_history_sync_now() == ESP_OK);
    assert(playback_history_pending_count() == 1);
}

void test_initialize_requires_history_and_is_idempotent() {
    playback_history_sync_host_reset_module();
    playback_history_sync_host_reset();
    playback_history_test_store_reset();
    playback_history_test_reset_ready();
    const module_descriptor_t *const descriptor =
        playback_history_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!playback_history_sync_host_is_ready());
    assert(playback_history_sync_host_task_create_calls() == 0);

    assert(playback_history_init() == ESP_OK);
    playback_history_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!playback_history_sync_host_is_ready());

    playback_history_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(playback_history_sync_host_is_ready());
    assert(playback_history_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(playback_history_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_with_short_interval() {
    reset_all();
    const module_descriptor_t *const descriptor =
        playback_history_sync_module_descriptor();
    playback_history_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    playback_history_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(playback_history_sync_host_task_pending());

    playback_history_sync_host_run_pending_task();
    assert(playback_history_sync_host_request_count() == 0);
    assert(playback_history_sync_host_delay_calls() == 1);
    assert(playback_history_sync_host_last_delay_ticks() == 15000u);
    assert(playback_history_sync_host_task_delete_calls() == 1);
    assert(!playback_history_sync_host_task_pending());
}

void test_worker_drains_then_uses_regular_interval() {
    reset_all();
    const module_descriptor_t *const descriptor =
        playback_history_sync_module_descriptor();
    queue_record("pkg-worker", 1, PLAYBACK_OUTCOME_COMPLETED, 10, 1000);
    playback_history_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    playback_history_sync_host_run_pending_task();
    assert(playback_history_sync_host_request_count() == 1);
    assert(playback_history_sync_host_delay_calls() == 1);
    assert(playback_history_sync_host_last_delay_ticks() == 120000u);
    assert(playback_history_sync_host_task_delete_calls() == 1);
    assert(playback_history_pending_count() == 0);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_ready_history_store();
    test_now_requires_trusted_runtime_state();
    test_successful_report_drains_queue_in_order();
    test_removed_package_is_discarded_and_queue_continues();
    test_transient_failure_stops_and_keeps_remaining_rows();
    test_unauthorized_and_revoked_clear_session();
    test_identity_and_transport_failures_keep_rows();
    test_transport_failure_keeps_rows();
    test_initialize_requires_history_and_is_idempotent();
    test_worker_retries_offline_with_short_interval();
    test_worker_drains_then_uses_regular_interval();
    return 0;
}
