#include "parent_policy.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "esp_http_client.h"
#include "parent_policy_host.h"
#include "transport_security.h"
#include "volume_control.h"

namespace {

const char *valid_policy_body(int64_t version) {
    static char body[1024];
    snprintf(
        body,
        sizeof(body),
        "{\"data\":{\"policy_version\":%lld,\"daily_limit_minutes\":120,"
        "\"allowed_categories\":[\"story\",\"english\"],"
        "\"disabled_periods\":[{\"start_time\":\"20:00\","
        "\"end_time\":\"21:30\"}],\"max_volume_percent\":55,"
        "\"updated_at\":\"2026-10-17T12:00:00Z\","
        "\"source_child_count\":2,\"aggregation_mode\":\"most_restrictive\","
        "\"schema_version\":\"1.0.0\"}}",
        (long long)version
    );
    return body;
}

void reset_all() {
    parent_policy_shutdown();
    parent_policy_host_reset_store();
    parent_policy_host_reset();
    parent_policy_host_reset_module();
    assert(parent_policy_init() == ESP_OK);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *const descriptor =
        parent_policy_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "parent_policy") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize == parent_policy_init);
    assert(descriptor->shutdown == parent_policy_shutdown);
}

void test_requires_initialization_and_dependencies() {
    parent_policy_shutdown();
    parent_policy_host_reset_store();
    parent_policy_host_reset();
    parent_policy_host_reset_module();
    assert(parent_policy_refresh() == ESP_ERR_INVALID_STATE);
    assert(parent_policy_refresh_if_due() == ESP_ERR_INVALID_STATE);
    assert(parent_policy_clear() == ESP_ERR_INVALID_STATE);

    parent_policy_host_set_store_ready(false);
    assert(parent_policy_init() == ESP_ERR_INVALID_STATE);
    assert(!parent_policy_is_ready());
}

void test_initializes_without_cached_policy_and_restores_volume() {
    parent_policy_shutdown();
    parent_policy_host_reset_store();
    parent_policy_host_reset();
    parent_policy_host_reset_module();
    assert(parent_policy_init() == ESP_OK);
    assert(parent_policy_is_ready());
    assert(parent_policy_host_volume_calls() == 1);
    assert(parent_policy_host_last_volume_percent() == 80);

    parent_policy_snapshot_t snapshot = {};
    assert(parent_policy_get_snapshot(&snapshot) == ESP_ERR_NOT_FOUND);
    const parent_policy_status_t status = parent_policy_get_status();
    assert(!status.has_policy);
    assert(!status.is_synchronized);
    assert(status.last_error == ESP_ERR_NOT_FOUND);
}

void test_successful_fetch_persists_and_applies_policy() {
    reset_all();
    parent_policy_host_set_http_response(200, valid_policy_body(7));

    assert(parent_policy_refresh() == ESP_OK);
    assert(parent_policy_host_request_count() == 1);
    assert(
        strcmp(
            parent_policy_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "runtime/parent-policy"
        ) == 0
    );
    assert(
        strcmp(
            parent_policy_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(parent_policy_host_request_method(0) == HTTP_METHOD_GET);
    assert(parent_policy_host_volume_calls() == 2);
    assert(parent_policy_host_last_volume_percent() == 55);

    parent_policy_snapshot_t snapshot = {};
    assert(parent_policy_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.policy_version == 7);
    assert(snapshot.daily_limit_minutes == 120);
    assert(snapshot.allowed_category_count == 2);
    assert(strcmp(snapshot.allowed_categories[0], "story") == 0);
    assert(strcmp(snapshot.allowed_categories[1], "english") == 0);
    assert(snapshot.disabled_period_count == 1);
    assert(strcmp(snapshot.disabled_periods[0].start_time, "20:00") == 0);
    assert(strcmp(snapshot.disabled_periods[0].end_time, "21:30") == 0);
    assert(snapshot.max_volume_percent == 55);
    assert(strcmp(snapshot.updated_at, "2026-10-17T12:00:00Z") == 0);

    assert(
        strcmp(parent_policy_host_store_get("pp_version"), "7") == 0
    );
    assert(
        strcmp(parent_policy_host_store_get("pp_daily_limit"), "120") == 0
    );
    assert(
        strcmp(
            parent_policy_host_store_get("pp_categories"),
            "story,english"
        ) == 0
    );
    assert(
        strcmp(
            parent_policy_host_store_get("pp_period_00"),
            "20:00-21:30"
        ) == 0
    );
    assert(
        strcmp(parent_policy_host_store_get("pp_max_volume"), "55") == 0
    );
    assert(parent_policy_get_status().is_synchronized);
}

void test_cached_policy_is_loaded_on_init() {
    reset_all();
    parent_policy_host_set_http_response(200, valid_policy_body(9));
    assert(parent_policy_refresh() == ESP_OK);
    parent_policy_shutdown();
    parent_policy_host_reset();
    parent_policy_host_reset_module();

    assert(parent_policy_init() == ESP_OK);
    parent_policy_snapshot_t snapshot = {};
    assert(parent_policy_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.policy_version == 9);
    assert(snapshot.max_volume_percent == 55);
    assert(parent_policy_host_last_volume_percent() == 55);
    assert(parent_policy_get_status().is_synchronized);
}

void test_equal_version_is_idempotent_and_does_not_rewrite_store() {
    reset_all();
    parent_policy_host_set_http_response(200, valid_policy_body(12));
    assert(parent_policy_refresh() == ESP_OK);
    const int writes = parent_policy_host_volume_calls();
    assert(writes == 2);

    parent_policy_host_set_http_response(200, valid_policy_body(12));
    assert(parent_policy_refresh() == ESP_OK);
    assert(parent_policy_host_volume_calls() == writes);
    assert(parent_policy_get_status().is_synchronized);
    assert(parent_policy_get_status().last_error == ESP_OK);
}

void test_stale_version_keeps_newer_policy() {
    reset_all();
    parent_policy_host_set_http_response(200, valid_policy_body(20));
    assert(parent_policy_refresh() == ESP_OK);
    parent_policy_host_set_http_response(200, valid_policy_body(19));

    assert(parent_policy_refresh() == ESP_ERR_INVALID_VERSION);
    parent_policy_snapshot_t snapshot = {};
    assert(parent_policy_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.policy_version == 20);
    assert(parent_policy_host_last_volume_percent() == 55);
    const parent_policy_status_t status = parent_policy_get_status();
    assert(status.has_policy);
    assert(!status.is_synchronized);
    assert(
        status.last_refresh_reason == PARENT_POLICY_REFRESH_REASON_VERSION
    );
    assert(status.last_error == ESP_ERR_INVALID_VERSION);
}

void test_malformed_success_response_keeps_cached_policy() {
    reset_all();
    parent_policy_host_set_http_response(200, valid_policy_body(3));
    assert(parent_policy_refresh() == ESP_OK);

    const char *const malformed[] = {
        "{not-json",
        "{}",
        "{\"data\":null}",
        "{\"data\":{\"policy_version\":4}}",
        "{\"data\":{\"policy_version\":4,\"daily_limit_minutes\":120,"
        "\"allowed_categories\":[\"story\"],\"disabled_periods\":[],"
        "\"max_volume_percent\":55,\"updated_at\":\"bad\","
        "\"schema_version\":\"1.0.0\",\"source_child_count\":1,"
        "\"aggregation_mode\":\"most_restrictive\"}}",
        "{\"data\":{\"policy_version\":4,\"daily_limit_minutes\":120,"
        "\"allowed_categories\":[\"unknown\"],\"disabled_periods\":[],"
        "\"max_volume_percent\":55,"
        "\"updated_at\":\"2026-10-17T12:00:00Z\","
        "\"schema_version\":\"1.0.0\",\"source_child_count\":1,"
        "\"aggregation_mode\":\"most_restrictive\"}}",
        "{\"data\":{\"policy_version\":4,\"daily_limit_minutes\":120,"
        "\"allowed_categories\":[\"story\"],"
        "\"disabled_periods\":[{\"start_time\":\"25:00\","
        "\"end_time\":\"26:00\"}],\"max_volume_percent\":55,"
        "\"updated_at\":\"2026-10-17T12:00:00Z\","
        "\"schema_version\":\"1.0.0\",\"source_child_count\":1,"
        "\"aggregation_mode\":\"most_restrictive\"}}",
    };
    for (const char *body : malformed) {
        parent_policy_host_set_http_response(200, body);
        assert(parent_policy_refresh() == ESP_ERR_INVALID_RESPONSE);
        parent_policy_snapshot_t snapshot = {};
        assert(parent_policy_get_snapshot(&snapshot) == ESP_OK);
        assert(snapshot.policy_version == 3);
    }
}

void test_oversized_response_is_capped() {
    reset_all();
    char oversized[9000];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    parent_policy_host_set_http_response(200, oversized);

    assert(parent_policy_refresh() == ESP_ERR_INVALID_RESPONSE);
    assert(parent_policy_get_snapshot(nullptr) == ESP_ERR_INVALID_ARG);
}

void test_not_found_with_error_code_clears_cache_and_volume() {
    reset_all();
    parent_policy_host_set_http_response(200, valid_policy_body(5));
    assert(parent_policy_refresh() == ESP_OK);

    parent_policy_host_set_http_response(
        404,
        "{\"error\":{\"code\":\"parent_policy_not_found\"}}"
    );
    assert(parent_policy_refresh() == ESP_OK);
    assert(parent_policy_host_last_volume_percent() == 80);
    assert(!parent_policy_host_store_has_key("pp_version"));
    parent_policy_snapshot_t snapshot = {};
    assert(
        parent_policy_get_snapshot(&snapshot) == ESP_ERR_NOT_FOUND
    );
    const parent_policy_status_t status = parent_policy_get_status();
    assert(!status.has_policy);
    assert(status.is_synchronized);
    assert(
        status.last_refresh_reason == PARENT_POLICY_REFRESH_REASON_NOT_FOUND
    );
}

void test_route_level_not_found_keeps_cached_policy() {
    reset_all();
    parent_policy_host_set_http_response(200, valid_policy_body(6));
    assert(parent_policy_refresh() == ESP_OK);
    parent_policy_host_set_http_response(404, "{}");

    assert(parent_policy_refresh() == ESP_ERR_NOT_FOUND);
    parent_policy_snapshot_t snapshot = {};
    assert(parent_policy_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.policy_version == 6);
    assert(parent_policy_host_last_volume_percent() == 55);
    const parent_policy_status_t status = parent_policy_get_status();
    assert(status.has_policy);
    assert(!status.is_synchronized);
    assert(
        status.last_refresh_reason == PARENT_POLICY_REFRESH_REASON_RESPONSE
    );
}

void test_unauthorized_and_revoked_clear_platform_session() {
    reset_all();
    parent_policy_host_set_http_response(401, "{}");
    assert(parent_policy_refresh() == ESP_ERR_INVALID_STATE);
    assert(parent_policy_host_unauthorized_calls() == 1);

    parent_policy_host_set_http_response(403, "{}");
    assert(parent_policy_refresh() == ESP_ERR_INVALID_STATE);
    assert(parent_policy_host_revoked_calls() == 1);
    const parent_policy_status_t status = parent_policy_get_status();
    assert(
        status.last_refresh_reason ==
        PARENT_POLICY_REFRESH_REASON_UNAUTHORIZED
    );
}

void test_identity_and_transport_failures_are_bounded() {
    reset_all();
    parent_policy_host_set_cloud_auth_result(ESP_ERR_INVALID_STATE);
    assert(parent_policy_refresh() == ESP_ERR_INVALID_STATE);
    assert(parent_policy_host_request_count() == 0);

    reset_all();
    parent_policy_host_set_copy_session_result(ESP_ERR_INVALID_STATE);
    assert(parent_policy_refresh() == ESP_ERR_INVALID_STATE);
    assert(parent_policy_host_request_count() == 0);

    reset_all();
    parent_policy_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(parent_policy_refresh() == ESP_ERR_INVALID_SIZE);
    assert(parent_policy_host_request_count() == 0);

    reset_all();
    parent_policy_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(parent_policy_refresh() == ESP_ERR_INVALID_SIZE);
    assert(parent_policy_host_request_count() == 0);

    reset_all();
    parent_policy_host_set_base_url("http://api.clarkhub.cn");
    assert(parent_policy_refresh() == ESP_ERR_INVALID_ARG);
    assert(parent_policy_host_request_count() == 0);

    reset_all();
    parent_policy_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_HOST_NOT_ALLOWED
    );
    assert(parent_policy_refresh() == ESP_ERR_INVALID_ARG);
    assert(parent_policy_host_request_count() == 0);
    assert(parent_policy_host_transport_security_calls() == 1);
}

void test_transport_failures_are_propagated() {
    reset_all();
    parent_policy_host_set_client_init_fails(true);
    assert(parent_policy_refresh() == ESP_FAIL);

    reset_all();
    parent_policy_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(parent_policy_refresh() == ESP_ERR_TIMEOUT);
    const parent_policy_status_t status = parent_policy_get_status();
    assert(
        status.last_refresh_reason == PARENT_POLICY_REFRESH_REASON_NETWORK
    );
}

void test_volume_apply_failure_keeps_persisted_policy() {
    reset_all();
    parent_policy_host_set_volume_result(VOLUME_CONTROL_ERR_STORAGE);
    parent_policy_host_set_http_response(200, valid_policy_body(30));

    assert(parent_policy_refresh() == ESP_ERR_INVALID_STATE);
    assert(strcmp(parent_policy_host_store_get("pp_version"), "30") == 0);
    parent_policy_snapshot_t snapshot = {};
    assert(
        parent_policy_get_snapshot(&snapshot) == ESP_ERR_NOT_FOUND
    );
    const parent_policy_status_t status = parent_policy_get_status();
    assert(!status.has_policy);
    assert(
        status.last_refresh_reason == PARENT_POLICY_REFRESH_REASON_VOLUME
    );
}

void test_store_failure_keeps_existing_policy() {
    reset_all();
    parent_policy_host_set_http_response(200, valid_policy_body(40));
    assert(parent_policy_refresh() == ESP_OK);
    parent_policy_host_set_store_fail_on_key("pp_version");
    parent_policy_host_set_http_response(200, valid_policy_body(41));

    assert(parent_policy_refresh() == ESP_FAIL);
    parent_policy_snapshot_t snapshot = {};
    assert(parent_policy_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.policy_version == 40);
    const parent_policy_status_t status = parent_policy_get_status();
    assert(
        status.last_refresh_reason == PARENT_POLICY_REFRESH_REASON_STORAGE
    );
}

void test_clear_erases_cache_and_restores_volume() {
    reset_all();
    parent_policy_host_set_http_response(200, valid_policy_body(50));
    assert(parent_policy_refresh() == ESP_OK);

    assert(parent_policy_clear() == ESP_OK);
    assert(!parent_policy_host_store_has_key("pp_version"));
    assert(!parent_policy_host_store_has_key("pp_categories"));
    assert(parent_policy_host_last_volume_percent() == 80);
    assert(parent_policy_host_volume_calls() == 3);
    parent_policy_snapshot_t snapshot = {};
    assert(
        parent_policy_get_snapshot(&snapshot) == ESP_ERR_NOT_FOUND
    );
}

void test_observer_receives_apply_and_clear() {
    reset_all();
    int callbacks = 0;
    parent_policy_change_callback_t callback = [](void *context) {
        ++*(int *)context;
    };
    assert(parent_policy_add_change_observer(callback, &callbacks) == ESP_OK);
    assert(parent_policy_add_change_observer(callback, &callbacks) == ESP_OK);

    parent_policy_host_set_http_response(200, valid_policy_body(60));
    assert(parent_policy_refresh() == ESP_OK);
    assert(callbacks == 1);

    assert(parent_policy_refresh() == ESP_OK);
    assert(callbacks == 1);

    parent_policy_host_set_http_response(
        404,
        "{\"error\":{\"code\":\"parent_policy_not_found\"}}"
    );
    assert(parent_policy_refresh() == ESP_OK);
    assert(callbacks == 2);

    assert(parent_policy_remove_change_observer(callback, &callbacks) ==
           ESP_OK);
    parent_policy_host_set_http_response(200, valid_policy_body(61));
    assert(parent_policy_refresh() == ESP_OK);
    assert(callbacks == 2);
}

void test_refresh_if_due_rate_limits_and_bypasses_when_needed() {
    reset_all();
    parent_policy_host_set_time_seconds(1000);
    parent_policy_host_set_http_response(200, valid_policy_body(70));
    assert(parent_policy_refresh() == ESP_OK);
    assert(parent_policy_host_request_count() == 1);

    parent_policy_host_set_time_seconds(1100);
    assert(parent_policy_refresh_if_due() == ESP_OK);
    assert(parent_policy_host_request_count() == 1);

    parent_policy_host_set_time_seconds(2000);
    assert(parent_policy_refresh_if_due() == ESP_OK);
    assert(parent_policy_host_request_count() == 2);

    reset_all();
    parent_policy_host_set_time_seconds(1000);
    parent_policy_host_set_http_response(500, "{}");
    assert(parent_policy_refresh() == ESP_ERR_INVALID_RESPONSE);
    parent_policy_host_set_time_seconds(1100);
    assert(parent_policy_refresh_if_due() == ESP_ERR_INVALID_RESPONSE);
    assert(parent_policy_host_request_count() == 1);
}

void test_refresh_reason_names_are_stable() {
    assert(strcmp(
               parent_policy_refresh_reason_name(
                   PARENT_POLICY_REFRESH_REASON_NONE
               ),
               "none"
           ) == 0);
    assert(strcmp(
               parent_policy_refresh_reason_name(
                   PARENT_POLICY_REFRESH_REASON_NOT_READY
               ),
               "not_ready"
           ) == 0);
    assert(strcmp(
               parent_policy_refresh_reason_name(
                   PARENT_POLICY_REFRESH_REASON_NETWORK
               ),
               "network"
           ) == 0);
    assert(strcmp(
               parent_policy_refresh_reason_name(
                   PARENT_POLICY_REFRESH_REASON_UNAUTHORIZED
               ),
               "unauthorized"
           ) == 0);
    assert(strcmp(
               parent_policy_refresh_reason_name(
                   PARENT_POLICY_REFRESH_REASON_NOT_FOUND
               ),
               "not_found"
           ) == 0);
    assert(strcmp(
               parent_policy_refresh_reason_name(
                   PARENT_POLICY_REFRESH_REASON_RESPONSE
               ),
               "invalid_response"
           ) == 0);
    assert(strcmp(
               parent_policy_refresh_reason_name(
                   PARENT_POLICY_REFRESH_REASON_VERSION
               ),
               "stale_version"
           ) == 0);
    assert(strcmp(
               parent_policy_refresh_reason_name(
                   PARENT_POLICY_REFRESH_REASON_STORAGE
               ),
               "storage"
           ) == 0);
    assert(strcmp(
               parent_policy_refresh_reason_name(
                   PARENT_POLICY_REFRESH_REASON_VOLUME
               ),
               "volume"
           ) == 0);
    assert(strcmp(
               parent_policy_refresh_reason_name(
                   PARENT_POLICY_REFRESH_REASON_INTERNAL
               ),
               "internal"
           ) == 0);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_requires_initialization_and_dependencies();
    test_initializes_without_cached_policy_and_restores_volume();
    test_successful_fetch_persists_and_applies_policy();
    test_cached_policy_is_loaded_on_init();
    test_equal_version_is_idempotent_and_does_not_rewrite_store();
    test_stale_version_keeps_newer_policy();
    test_malformed_success_response_keeps_cached_policy();
    test_oversized_response_is_capped();
    test_not_found_with_error_code_clears_cache_and_volume();
    test_route_level_not_found_keeps_cached_policy();
    test_unauthorized_and_revoked_clear_platform_session();
    test_identity_and_transport_failures_are_bounded();
    test_transport_failures_are_propagated();
    test_volume_apply_failure_keeps_persisted_policy();
    test_store_failure_keeps_existing_policy();
    test_clear_erases_cache_and_restores_volume();
    test_observer_receives_apply_and_clear();
    test_refresh_if_due_rate_limits_and_bypasses_when_needed();
    test_refresh_reason_names_are_stable();
    return 0;
}
