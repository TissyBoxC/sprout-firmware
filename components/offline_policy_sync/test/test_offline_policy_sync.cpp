#include "offline_policy_sync.h"

#include <cassert>
#include <cstring>

#include "esp_http_client.h"
#include "offline_policy.h"
#include "offline_policy_sync_host.h"
#include "transport_security.h"

namespace {

void reset_all() {
    offline_policy_sync_host_reset_module();
    offline_policy_sync_host_reset();
    offline_policy_sync_host_reset_store();
    offline_policy_test_reset_ready();
    assert(offline_policy_init() == ESP_OK);
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    offline_policy_sync_host_set_http_sequence(statuses, bodies, 1);
}

void set_apply_and_ack_response(const char *policy_body) {
    const int statuses[] = {200, 200};
    const char *bodies[] = {policy_body, "{}"};
    offline_policy_sync_host_set_http_sequence(statuses, bodies, 2);
}

const char *published_policy_body() {
    return "{\"data\":{\"offline_enabled\":true,"
           "\"download_over_wifi_only\":false,\"max_cached_packages\":40,"
           "\"max_cache_bytes\":1073741824,\"cache_ttl_hours\":240,"
           "\"revision\":7}}";
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        offline_policy_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "offline_policy_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_initialized_store() {
    offline_policy_sync_host_reset();
    offline_policy_sync_host_reset_module();
    offline_policy_sync_host_reset_store();
    offline_policy_test_reset_ready();
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(offline_policy_sync_host_request_count() == 0);
}

void test_now_requires_trusted_runtime_state() {
    reset_all();
    offline_policy_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);

    offline_policy_sync_host_set_network_state(NETWORK_MANAGER_STATE_CONNECTED);
    offline_policy_sync_host_set_time_synchronized(false);
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);

    offline_policy_sync_host_set_time_synchronized(true);
    offline_policy_sync_host_set_registered(false);
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(offline_policy_sync_host_request_count() == 0);
}

void test_now_rejects_unusable_identity_and_base_url() {
    reset_all();
    offline_policy_sync_host_set_copy_session_result(ESP_ERR_INVALID_SIZE);
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(offline_policy_sync_host_request_count() == 0);

    reset_all();
    offline_policy_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(offline_policy_sync_host_request_count() == 0);

    reset_all();
    offline_policy_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(offline_policy_sync_host_request_count() == 0);

    reset_all();
    offline_policy_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(offline_policy_sync_host_request_count() == 0);
}

void test_successful_fetch_applies_policy_and_acknowledges_revision() {
    reset_all();
    set_apply_and_ack_response(published_policy_body());

    assert(offline_policy_sync_now() == ESP_OK);
    assert(offline_policy_sync_host_request_count() == 2);
    assert(
        strcmp(
            offline_policy_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "content/offline-policy"
        ) == 0
    );
    assert(
        strcmp(
            offline_policy_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(offline_policy_sync_host_request_method(0) == HTTP_METHOD_GET);

    const offline_policy_t applied = offline_policy_get();
    assert(applied.revision == 7u);
    assert(applied.offline_enabled);
    assert(!applied.download_over_wifi_only);
    assert(applied.max_cached_packages == 40u);
    assert(applied.max_cache_bytes == 1024ll * 1024ll * 1024ll);
    assert(applied.cache_ttl_hours == 240u);

    assert(
        strcmp(
            offline_policy_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "content/offline-policy/acknowledgement"
        ) == 0
    );
    assert(offline_policy_sync_host_request_method(1) == HTTP_METHOD_POST);
    assert(
        strcmp(
            offline_policy_sync_host_request_content_type(1),
            "application/json"
        ) == 0
    );
    assert(
        strcmp(
            offline_policy_sync_host_request_body(1),
            "{\"revision\":7}"
        ) == 0
    );
}

void test_absent_optional_fields_keep_applied_policy() {
    reset_all();
    offline_policy_t seeded = offline_policy_default();
    seeded.revision = 5u;
    seeded.offline_enabled = false;
    seeded.download_over_wifi_only = false;
    seeded.max_cached_packages = 55u;
    seeded.cache_ttl_hours = 333u;
    assert(offline_policy_set(&seeded) == ESP_OK);

    set_apply_and_ack_response(
        "{\"data\":{\"max_cache_bytes\":268435456,\"revision\":6}}"
    );
    assert(offline_policy_sync_now() == ESP_OK);

    const offline_policy_t applied = offline_policy_get();
    assert(applied.revision == 6u);
    assert(!applied.offline_enabled);
    assert(!applied.download_over_wifi_only);
    assert(applied.max_cached_packages == 55u);
    assert(applied.max_cache_bytes == 256ll * 1024ll * 1024ll);
    assert(applied.cache_ttl_hours == 333u);
}

void test_stale_revision_is_not_rolled_back() {
    reset_all();
    offline_policy_t seeded = offline_policy_default();
    seeded.revision = 9u;
    seeded.max_cached_packages = 60u;
    assert(offline_policy_set(&seeded) == ESP_OK);

    set_apply_and_ack_response(
        "{\"data\":{\"max_cached_packages\":3,"
        "\"max_cache_bytes\":268435456,\"revision\":4}}"
    );
    assert(offline_policy_sync_now() == ESP_OK);

    const offline_policy_t applied = offline_policy_get();
    assert(applied.revision == 9u);
    assert(applied.max_cached_packages == 60u);
    // The acknowledgement reports the revision actually applied.
    assert(
        strcmp(
            offline_policy_sync_host_request_body(1),
            "{\"revision\":9}"
        ) == 0
    );
}

void test_malformed_responses_leave_policy_untouched() {
    reset_all();
    offline_policy_t seeded = offline_policy_default();
    seeded.revision = 4u;
    seeded.max_cached_packages = 33u;
    assert(offline_policy_set(&seeded) == ESP_OK);

    set_single_response(200, "{not-json");
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(offline_policy_get().revision == 4u);
    assert(offline_policy_get().max_cached_packages == 33u);

    set_single_response(200, "{}");
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(offline_policy_get().revision == 4u);

    set_single_response(200, "{\"data\":{\"revision\":8}}");
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(offline_policy_get().revision == 4u);

    set_single_response(
        200,
        "{\"data\":{\"max_cache_bytes\":-1,\"revision\":8}}"
    );
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(offline_policy_get().revision == 4u);

    set_single_response(500, "{}");
    assert(offline_policy_sync_now() == ESP_FAIL);
    assert(offline_policy_get().revision == 4u);
}

void test_oversized_response_is_rejected() {
    reset_all();
    offline_policy_t seeded = offline_policy_default();
    seeded.revision = 4u;
    assert(offline_policy_set(&seeded) == ESP_OK);
    static char oversized[4200];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';

    set_single_response(200, oversized);
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(offline_policy_sync_host_request_count() == 1);
    assert(offline_policy_get().revision == 4u);
}

void test_unauthorized_fetch_clears_session() {
    reset_all();
    set_single_response(401, "{}");
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(offline_policy_sync_host_clear_session_calls() == 1);

    reset_all();
    set_single_response(403, "{}");
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(offline_policy_sync_host_clear_session_calls() == 1);
}

void test_transport_failures_are_propagated() {
    reset_all();
    offline_policy_sync_host_set_client_init_fails(true);
    assert(offline_policy_sync_now() == ESP_ERR_NO_MEM);

    reset_all();
    offline_policy_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    offline_policy_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(offline_policy_sync_host_request_count() == 0);
    assert(offline_policy_sync_host_transport_security_calls() == 1);
}

void test_acknowledgement_failure_is_propagated() {
    reset_all();
    const int statuses[] = {200, 500};
    const char *bodies[] = {published_policy_body(), "{}"};
    offline_policy_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(offline_policy_sync_now() == ESP_FAIL);
    assert(offline_policy_sync_host_request_count() == 2);
    assert(offline_policy_get().revision == 7u);
}

void test_unauthorized_acknowledgement_clears_session() {
    reset_all();
    const int statuses[] = {200, 401};
    const char *bodies[] = {published_policy_body(), "{}"};
    offline_policy_sync_host_set_http_sequence(statuses, bodies, 2);

    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(offline_policy_sync_host_clear_session_calls() == 1);
    assert(offline_policy_get().revision == 7u);

    reset_all();
    const int forbidden_statuses[] = {200, 403};
    offline_policy_sync_host_set_http_sequence(
        forbidden_statuses,
        bodies,
        2
    );
    assert(offline_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(offline_policy_sync_host_clear_session_calls() == 1);
    assert(offline_policy_get().revision == 7u);
}

void test_initialize_requires_store_and_is_idempotent() {
    offline_policy_sync_host_reset();
    offline_policy_sync_host_reset_module();
    offline_policy_sync_host_reset_store();
    offline_policy_test_reset_ready();
    const module_descriptor_t *descriptor =
        offline_policy_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!offline_policy_sync_host_is_ready());
    assert(offline_policy_sync_host_task_create_calls() == 0);

    assert(offline_policy_init() == ESP_OK);
    offline_policy_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!offline_policy_sync_host_is_ready());

    offline_policy_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(offline_policy_sync_host_is_ready());
    assert(offline_policy_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(offline_policy_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_with_short_interval() {
    reset_all();
    const module_descriptor_t *descriptor =
        offline_policy_sync_module_descriptor();
    offline_policy_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    offline_policy_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(offline_policy_sync_host_task_pending());

    offline_policy_sync_host_run_pending_task();
    assert(offline_policy_sync_host_request_count() == 0);
    assert(offline_policy_sync_host_delay_calls() == 1);
    assert(offline_policy_sync_host_last_delay_ticks() == 15000u);
    assert(offline_policy_sync_host_task_delete_calls() == 1);
    assert(!offline_policy_sync_host_task_pending());
}

void test_worker_syncs_then_uses_regular_interval() {
    reset_all();
    const module_descriptor_t *descriptor =
        offline_policy_sync_module_descriptor();
    set_apply_and_ack_response(published_policy_body());
    offline_policy_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    offline_policy_sync_host_run_pending_task();
    assert(offline_policy_sync_host_request_count() == 2);
    assert(offline_policy_sync_host_delay_calls() == 1);
    assert(offline_policy_sync_host_last_delay_ticks() == 1800000u);
    assert(offline_policy_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_initialized_store();
    test_now_requires_trusted_runtime_state();
    test_now_rejects_unusable_identity_and_base_url();
    test_successful_fetch_applies_policy_and_acknowledges_revision();
    test_absent_optional_fields_keep_applied_policy();
    test_stale_revision_is_not_rolled_back();
    test_malformed_responses_leave_policy_untouched();
    test_oversized_response_is_rejected();
    test_unauthorized_fetch_clears_session();
    test_transport_failures_are_propagated();
    test_acknowledgement_failure_is_propagated();
    test_unauthorized_acknowledgement_clears_session();
    test_initialize_requires_store_and_is_idempotent();
    test_worker_retries_offline_with_short_interval();
    test_worker_syncs_then_uses_regular_interval();
    return 0;
}
