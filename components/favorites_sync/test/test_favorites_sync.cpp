#include "favorites_sync.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "favorites.h"
#include "favorites_sync_host.h"
#include "esp_http_client.h"
#include "transport_security.h"

namespace {

void reset_all() {
    favorites_sync_host_reset_module();
    favorites_sync_host_reset();
    favorites_sync_host_reset_store();
    favorites_test_reset_ready();
    assert(favorites_init() == ESP_OK);
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    favorites_sync_host_set_http_sequence(statuses, bodies, 1);
}

void seed_favorite(const char *package_id) {
    assert(favorites_add(package_id) == ESP_OK);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        favorites_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "favorites_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_initialized_store() {
    favorites_sync_host_reset();
    favorites_sync_host_reset_module();
    favorites_sync_host_reset_store();
    favorites_test_reset_ready();
    assert(favorites_sync_now() == ESP_ERR_INVALID_STATE);
    assert(favorites_sync_host_request_count() == 0);
}

void test_now_requires_trusted_runtime_state() {
    reset_all();
    favorites_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(favorites_sync_now() == ESP_ERR_INVALID_STATE);

    favorites_sync_host_set_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    favorites_sync_host_set_time_synchronized(false);
    assert(favorites_sync_now() == ESP_ERR_INVALID_STATE);

    favorites_sync_host_set_time_synchronized(true);
    favorites_sync_host_set_registered(false);
    assert(favorites_sync_now() == ESP_ERR_INVALID_STATE);
    assert(favorites_sync_host_request_count() == 0);
}

void test_now_rejects_unusable_identity_and_base_url() {
    reset_all();
    favorites_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(favorites_sync_now() == ESP_ERR_INVALID_STATE);
    assert(favorites_sync_host_request_count() == 0);

    reset_all();
    favorites_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(favorites_sync_now() == ESP_ERR_INVALID_ARG);
    assert(favorites_sync_host_request_count() == 0);

    reset_all();
    favorites_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(favorites_sync_now() == ESP_ERR_INVALID_ARG);
    assert(favorites_sync_host_request_count() == 0);
}

void test_successful_fetch_replaces_favorites() {
    reset_all();
    seed_favorite("content_old");
    set_single_response(
        200,
        "{\"data\":{\"favorites\":[{\"package_id\":\"content_story_002\"},"
        "{\"package_id\":\"content_story_001\"}]}}"
    );

    assert(favorites_sync_now() == ESP_OK);
    assert(favorites_sync_host_request_count() == 1);
    assert(
        strcmp(
            favorites_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "content/favorites"
        ) == 0
    );
    assert(
        strcmp(
            favorites_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        favorites_sync_host_request_method(0) == HTTP_METHOD_GET
    );
    assert(favorites_count() == 2);
    assert(!favorites_contains("content_old"));
    assert(favorites_contains("content_story_002"));
    assert(favorites_contains("content_story_001"));

    char listed[FAVORITES_CAPACITY][CONTENT_LIBRARY_ID_SIZE] = {};
    size_t listed_count = 0;
    assert(
        favorites_list(listed, FAVORITES_CAPACITY, &listed_count) == ESP_OK
    );
    assert(listed_count == 2);
    assert(strcmp(listed[0], "content_story_002") == 0);
}

void test_empty_snapshot_clears_favorites() {
    reset_all();
    seed_favorite("content_old");
    set_single_response(200, "{\"data\":{\"favorites\":[]}}");

    assert(favorites_sync_now() == ESP_OK);
    assert(favorites_count() == 0);
}

void test_malformed_responses_leave_cache_untouched() {
    reset_all();
    seed_favorite("content_local");

    set_single_response(200, "{not-json");
    assert(favorites_sync_now() == ESP_ERR_INVALID_ARG);
    assert(favorites_count() == 1);

    set_single_response(200, "{\"data\":{\"favorites\":[{\"package_id\":\"\"}]}}");
    assert(favorites_sync_now() == ESP_ERR_INVALID_ARG);
    assert(favorites_count() == 1);

    set_single_response(500, "{}");
    assert(favorites_sync_now() == ESP_FAIL);
    assert(favorites_count() == 1);
}

void test_oversized_snapshot_returns_full_and_leaves_cache_untouched() {
    reset_all();
    seed_favorite("content_local");

    char body[4096] = {};
    size_t used = 0;
    const int prefix = snprintf(
        body,
        sizeof(body),
        "{\"data\":{\"favorites\":["
    );
    assert(prefix > 0);
    used = (size_t)prefix;
    for (int index = 0; index < FAVORITES_CAPACITY + 1; ++index) {
        const int written = snprintf(
            body + used,
            sizeof(body) - used,
            "%s{\"package_id\":\"content_%03d\"}",
            index == 0 ? "" : ",",
            index
        );
        assert(written > 0);
        used += (size_t)written;
        assert(used < sizeof(body));
    }
    assert(used + 4 < sizeof(body));
    snprintf(body + used, sizeof(body) - used, "]}}");

    set_single_response(200, body);
    assert(favorites_sync_now() == ESP_ERR_INVALID_STATE);
    assert(favorites_count() == 1);
    assert(favorites_contains("content_local"));
}

void test_unauthorized_fetch_clears_session() {
    reset_all();
    set_single_response(401, "{}");
    assert(favorites_sync_now() == ESP_ERR_INVALID_STATE);
    assert(favorites_sync_host_clear_session_calls() == 1);

    reset_all();
    set_single_response(403, "{}");
    assert(favorites_sync_now() == ESP_ERR_INVALID_STATE);
    assert(favorites_sync_host_clear_session_calls() == 1);
}

void test_transport_failures_are_propagated() {
    reset_all();
    favorites_sync_host_set_client_init_fails(true);
    assert(favorites_sync_now() == ESP_ERR_NO_MEM);

    reset_all();
    favorites_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(favorites_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    favorites_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(favorites_sync_now() == ESP_ERR_INVALID_ARG);
    assert(favorites_sync_host_request_count() == 0);
}

void test_initialize_requires_store_and_is_idempotent() {
    favorites_sync_host_reset();
    favorites_sync_host_reset_module();
    favorites_sync_host_reset_store();
    favorites_test_reset_ready();
    const module_descriptor_t *descriptor =
        favorites_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!favorites_sync_host_is_ready());
    assert(favorites_sync_host_task_create_calls() == 0);

    assert(favorites_init() == ESP_OK);
    favorites_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!favorites_sync_host_is_ready());

    favorites_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(favorites_sync_host_is_ready());
    assert(favorites_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(favorites_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_and_stops_cleanly() {
    reset_all();
    const module_descriptor_t *descriptor =
        favorites_sync_module_descriptor();
    favorites_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    favorites_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(favorites_sync_host_task_pending());

    favorites_sync_host_run_pending_task();
    assert(favorites_sync_host_request_count() == 0);
    assert(favorites_sync_host_delay_calls() == 1);
    assert(favorites_sync_host_task_delete_calls() == 1);
    assert(!favorites_sync_host_task_pending());
}

void test_worker_syncs_then_stops_cleanly() {
    reset_all();
    const module_descriptor_t *descriptor =
        favorites_sync_module_descriptor();
    set_single_response(200, "{\"data\":{\"favorites\":[]}}");
    favorites_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    favorites_sync_host_run_pending_task();
    assert(favorites_sync_host_request_count() == 1);
    assert(favorites_sync_host_delay_calls() == 1);
    assert(favorites_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_initialized_store();
    test_now_requires_trusted_runtime_state();
    test_now_rejects_unusable_identity_and_base_url();
    test_successful_fetch_replaces_favorites();
    test_empty_snapshot_clears_favorites();
    test_malformed_responses_leave_cache_untouched();
    test_oversized_snapshot_returns_full_and_leaves_cache_untouched();
    test_unauthorized_fetch_clears_session();
    test_transport_failures_are_propagated();
    test_initialize_requires_store_and_is_idempotent();
    test_worker_retries_offline_and_stops_cleanly();
    test_worker_syncs_then_stops_cleanly();
    return 0;
}
