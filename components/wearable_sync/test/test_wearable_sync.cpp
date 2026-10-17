#include "wearable_sync.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "esp_http_client.h"
#include "network_manager.h"
#include "wearable_sync_host.h"

namespace {

extern "C" int wearable_sync_test_config_store_write_calls;

void reset_all() {
    wearable_sync_host_reset_module();
    wearable_sync_host_reset();
    wearable_sync_test_config_store_write_calls = 0;
    (void)wearable_sync_register_provider(nullptr);
    assert(wearable_init() == ESP_OK);
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    wearable_sync_host_set_http_sequence(statuses, bodies, 1);
}

char policy_response_buffer[512];

const char *policy_response(
    bool enabled,
    bool permission_granted,
    const char *precision = "balanced",
    unsigned int upload_interval_seconds = 600u
) {
    snprintf(
        policy_response_buffer,
        sizeof(policy_response_buffer),
        "{\"data\":{"
        "\"schema_version\":1,"
        "\"enabled\":%s,"
        "\"permission_granted\":%s,"
        "\"precision\":\"%s\","
        "\"upload_interval_seconds\":%u,"
    "\"retention_days\":7,"
    "\"geofence_radius_meters\":500"
    "}}",
        enabled ? "true" : "false",
        permission_granted ? "true" : "false",
        precision,
        upload_interval_seconds
    );
    return policy_response_buffer;
}

void set_policy_enabled() {
    location_policy_t enabled = wearable_get();
    enabled.enabled = 1u;
    enabled.permission_granted = 1u;
    enabled.precision = LOCATION_PRECISION_BALANCED;
    enabled.upload_interval_seconds = 600u;
    enabled.retention_days = 7u;
    enabled.geofence_radius_meters = 500u;
    (void)wearable_set(&enabled);
}

void set_policy_location_sos_responses(int sos_status = 201) {
    const int statuses[] = {200, sos_status, 200};
    const char *bodies[] = {
        policy_response(true, true),
        "{\"data\":{\"state\":\"triggered\"}}",
        "{\"data\":{\"recorded\":true}}",
    };
    wearable_sync_host_set_http_sequence(statuses, bodies, 3);
}

esp_err_t valid_position(void *context, wearable_sync_position_t *position) {
    (void)context;
    position->latitude = 1.3521;
    position->longitude = 103.8198;
    position->accuracy_meters = 12.5;
    return ESP_OK;
}

esp_err_t no_fix(void *context, wearable_sync_position_t *position) {
    (void)context;
    (void)position;
    return ESP_ERR_NOT_FOUND;
}

void test_descriptor_is_stable() {
    const module_descriptor_t *const descriptor =
        wearable_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "wearable_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_wearable_runtime_and_connectivity() {
    reset_all();
    set_single_response(200, policy_response(false, false));
    const location_policy_t policy = wearable_get();
    assert(policy.permission_granted == 0u);
    assert(wearable_sync_now() == ESP_OK);
    assert(wearable_sync_host_request_count() == 1);

    reset_all();
    wearable_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(wearable_sync_now() == ESP_ERR_INVALID_STATE);
    assert(wearable_sync_host_request_count() == 0);

    reset_all();
    wearable_sync_host_set_time_synchronized(false);
    assert(wearable_sync_now() == ESP_ERR_INVALID_STATE);
    assert(wearable_sync_host_request_count() == 0);

    reset_all();
    wearable_sync_host_set_registered(false);
    assert(wearable_sync_now() == ESP_ERR_INVALID_STATE);
    assert(wearable_sync_host_request_count() == 0);
}

void test_prepare_propagates_identity_and_base_url_failures() {
    reset_all();
    wearable_sync_host_set_copy_session_result(ESP_ERR_INVALID_STATE);
    assert(wearable_sync_now() == ESP_ERR_INVALID_STATE);
    assert(wearable_sync_host_request_count() == 0);

    reset_all();
    wearable_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(wearable_sync_now() == ESP_ERR_INVALID_SIZE);
    assert(wearable_sync_host_request_count() == 0);

    reset_all();
    wearable_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(wearable_sync_now() == ESP_ERR_INVALID_SIZE);
    assert(wearable_sync_host_request_count() == 0);

    reset_all();
    wearable_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(wearable_sync_now() == ESP_ERR_INVALID_ARG);
    assert(wearable_sync_host_request_count() == 0);
}

void test_policy_location_and_sos_close_loop() {
    reset_all();
    const wearable_sync_provider_t provider = {
        .read = valid_position,
        .context = nullptr,
    };
    assert(wearable_sync_register_provider(&provider) == ESP_OK);
    set_policy_enabled();
    wearable_sync_host_set_sos_triggered(true);
    set_policy_location_sos_responses();

    assert(wearable_sync_now() == ESP_OK);
    assert(wearable_sync_host_request_count() == 3);
    assert(wearable_sync_host_request_method(0) == HTTP_METHOD_GET);
    assert(wearable_sync_host_request_method(1) == HTTP_METHOD_POST);
    assert(wearable_sync_host_request_method(2) == HTTP_METHOD_POST);

    assert(
        std::strcmp(
            wearable_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "wearable/policy"
        ) == 0
    );
    assert(
        std::strcmp(
            wearable_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "wearable/sos"
        ) == 0
    );
    assert(
        std::strcmp(
            wearable_sync_host_request_url(2),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "wearable/location"
        ) == 0
    );
    assert(
        std::strcmp(
            wearable_sync_host_request_authorization(2),
            "Bearer session-1"
        ) == 0
    );
    assert(
        std::strcmp(
            wearable_sync_host_request_accept(2),
            "application/json"
        ) == 0
    );
    assert(
        std::strcmp(
            wearable_sync_host_request_content_type(2),
            "application/json"
        ) == 0
    );

    const char *const location_body = wearable_sync_host_request_body(2);
    assert(std::strstr(location_body, "\"latitude\":1.3521") != nullptr);
    assert(std::strstr(location_body, "\"longitude\":103.8198") != nullptr);
    assert(std::strstr(location_body, "\"accuracy_meters\":12.5") != nullptr);
    assert(std::strstr(location_body, "\"precision\":\"balanced\"") != nullptr);
    assert(std::strstr(location_body, "\"geofence_state\":\"unknown\"") != nullptr);

    const location_policy_t applied = wearable_get();
    assert(applied.enabled == 1u);
    assert(applied.permission_granted == 1u);
    assert(applied.precision == LOCATION_PRECISION_BALANCED);
    assert(applied.upload_interval_seconds == 600u);
    assert(applied.retention_days == 7u);
    assert(applied.geofence_radius_meters == 500u);
    // The remote policy must be persisted, not merely held in RAM.
    assert(wearable_sync_host_policy_store_calls() >= 1);
}

void test_policy_rejects_missing_fields_and_invalid_values() {
    const char *const responses[] = {
        "{\"data\":{}}",
        "{\"data\":{\"schema_version\":1,\"enabled\":true,"
        "\"permission_granted\":true,\"precision\":\"exact\","
        "\"upload_interval_seconds\":600,\"retention_days\":30,"
        "\"geofence_radius_meters\":500}}",
        "{\"data\":{\"schema_version\":2,\"enabled\":true,"
        "\"permission_granted\":true,\"precision\":\"balanced\","
        "\"upload_interval_seconds\":600,\"retention_days\":30,"
        "\"geofence_radius_meters\":500}}",
    };
    for (const char *const body : responses) {
        reset_all();
        set_single_response(200, body);
        assert(wearable_sync_now() == ESP_ERR_INVALID_ARG);
        assert(wearable_sync_host_policy_store_calls() == 0);
    }

    reset_all();
    set_single_response(200, "not-json");
    assert(wearable_sync_now() == ESP_ERR_INVALID_ARG);
}

void test_revoked_policy_stops_reporting() {
    reset_all();
    const wearable_sync_provider_t provider = {
        .read = valid_position,
        .context = nullptr,
    };
    assert(wearable_sync_register_provider(&provider) == ESP_OK);
    set_policy_enabled();
    wearable_sync_host_set_sos_triggered(true);
    set_single_response(
        200,
        "{\"data\":{\"schema_version\":1,\"enabled\":false,"
        "\"permission_granted\":false,\"precision\":\"coarse\","
        "\"upload_interval_seconds\":600,\"retention_days\":30,"
        "\"geofence_radius_meters\":500}}"
    );
    assert(wearable_sync_now() == ESP_OK);
    assert(wearable_sync_host_request_count() == 1);
    assert(!wearable_sos_active());
    assert(wearable_sync_host_policy_store_calls() >= 1);
}

void test_policy_store_failure_is_propagated() {
    reset_all();
    location_policy_t current = wearable_get();
    current.upload_interval_seconds = 300u;
    assert(wearable_set(&current) == ESP_OK);
    wearable_sync_host_reset();
    wearable_sync_host_set_policy_store_fails(true);
    set_single_response(200, policy_response(true, true));
    assert(wearable_sync_now() == ESP_ERR_INVALID_STATE);
    assert(wearable_sync_host_request_count() == 1);
}

void test_upload_interval_controls_location_reports() {
    reset_all();
    set_policy_enabled();
    const wearable_sync_provider_t provider = {
        .read = valid_position,
        .context = nullptr,
    };
    assert(wearable_sync_register_provider(&provider) == ESP_OK);
    const int first_statuses[] = {200, 200};
    const char *first_bodies[] = {
        policy_response(true, true),
        "{}",
    };
    wearable_sync_test_config_store_write_calls = 0;
    wearable_sync_host_set_http_sequence(first_statuses, first_bodies, 2);
    assert(wearable_sync_now() == ESP_OK);
    assert(wearable_get().enabled == 1u);
    assert(wearable_get().upload_interval_seconds == 600u);
    assert(wearable_sync_host_request_count() == 2);
    assert(wearable_sync_host_policy_store_calls() == 0);

    // A policy refresh happens every cycle, but the unchanged position must
    // not upload before the guardian interval has elapsed.
    const int second_statuses[] = {200};
    const char *second_bodies[] = {policy_response(true, true)};
    wearable_sync_host_set_http_sequence(second_statuses, second_bodies, 1);
    wearable_sync_host_advance_time_ms(599000);
    assert(wearable_sync_now() == ESP_OK);
    assert(wearable_sync_host_request_count() == 1);
    assert(wearable_sync_host_policy_store_calls() == 0);

    const int third_statuses[] = {200, 200};
    const char *third_bodies[] = {policy_response(true, true), "{}"};
    wearable_sync_host_set_http_sequence(third_statuses, third_bodies, 2);
    wearable_sync_host_advance_time_ms(1000);
    assert(wearable_sync_now() == ESP_OK);
    assert(wearable_sync_host_request_count() == 2);
    assert(wearable_sync_host_policy_store_calls() == 0);
}

void test_sos_failure_is_not_hidden_by_location_success() {
    reset_all();
    const wearable_sync_provider_t provider = {
        .read = valid_position,
        .context = nullptr,
    };
    assert(wearable_sync_register_provider(&provider) == ESP_OK);
    set_policy_enabled();
    wearable_sync_host_set_sos_triggered(true);
    set_policy_location_sos_responses(500);

    assert(wearable_sync_now() == ESP_FAIL);
    assert(wearable_sync_host_request_count() == 3);
    assert(wearable_sos_active());

    // Restore the latched emergency and provide a successful retry after the
    // dedicated retry interval.
    wearable_sync_host_set_sos_triggered(true);
    const int statuses[] = {200, 201};
    const char *bodies[] = {
        policy_response(true, true),
        "{}",
    };
    wearable_sync_host_set_http_sequence(statuses, bodies, 2);
    wearable_sync_host_advance_time_ms(15000);
    assert(wearable_sync_now() == ESP_OK);
    assert(wearable_sync_host_request_count() == 2);
    assert(
        std::strcmp(
            wearable_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "wearable/policy"
        ) == 0
    );
    assert(
        std::strcmp(
            wearable_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "wearable/sos"
        ) == 0
    );
}

void test_unauthorized_clears_session() {
    for (const int status : {401, 403}) {
        reset_all();
        set_single_response(status, "{}");
        assert(wearable_sync_now() == ESP_ERR_INVALID_STATE);
        assert(wearable_sync_host_clear_session_calls() == 1);
    }
}

void test_transport_and_request_setup_failures_are_clean() {
    reset_all();
    wearable_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(wearable_sync_now() == ESP_ERR_INVALID_STATE);
    assert(wearable_sync_host_request_count() == 1);

    reset_all();
    wearable_sync_host_set_client_init_fails(true);
    assert(wearable_sync_now() == ESP_ERR_NO_MEM);
    assert(wearable_sync_host_request_count() == 0);

    reset_all();
    wearable_sync_host_set_header_failure("Authorization", ESP_ERR_INVALID_ARG);
    assert(wearable_sync_now() == ESP_ERR_INVALID_STATE);
    assert(wearable_sync_host_request_count() == 1);

    reset_all();
    wearable_sync_host_set_header_failure("Accept", ESP_ERR_INVALID_ARG);
    assert(wearable_sync_now() == ESP_ERR_INVALID_STATE);
    assert(wearable_sync_host_request_count() == 1);

    reset_all();
    wearable_sync_host_set_post_field_failure(ESP_ERR_INVALID_ARG);
    const wearable_sync_provider_t provider = {
        .read = valid_position,
        .context = nullptr,
    };
    assert(wearable_sync_register_provider(&provider) == ESP_OK);
    set_policy_location_sos_responses();
    assert(wearable_sync_now() == ESP_ERR_INVALID_STATE);
    assert(wearable_sync_host_request_count() == 2);
}

void test_no_provider_or_invalid_fix_skips_uploads() {
    reset_all();
    set_single_response(200, policy_response(true, true));
    assert(wearable_sync_now() == ESP_OK);
    assert(wearable_sync_host_request_count() == 1);

    reset_all();
    const wearable_sync_provider_t provider = {
        .read = no_fix,
        .context = nullptr,
    };
    assert(wearable_sync_register_provider(&provider) == ESP_OK);
    set_single_response(200, policy_response(true, true));
    assert(wearable_sync_now() == ESP_OK);
    assert(wearable_sync_host_request_count() == 1);
}

void test_initialize_requires_location_and_is_idempotent() {
    reset_all();
    const module_descriptor_t *const descriptor =
        wearable_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_OK);
    assert(wearable_sync_host_is_ready());
    assert(wearable_sync_host_task_create_calls() == 1);

    wearable_sync_host_reset();
    assert(descriptor->initialize() == ESP_OK);
    assert(wearable_sync_host_task_create_calls() == 0);
}

void test_worker_uses_retry_and_regular_intervals() {
    reset_all();
    const module_descriptor_t *const descriptor =
        wearable_sync_module_descriptor();
    wearable_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    wearable_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    wearable_sync_host_run_pending_task();
    assert(wearable_sync_host_request_count() == 0);
    assert(wearable_sync_host_delay_calls() == 1);
    assert(wearable_sync_host_last_delay_ticks() == 15000u);
    assert(wearable_sync_host_task_delete_calls() == 1);

    reset_all();
    set_single_response(
        200,
        "{\"data\":{\"schema_version\":1,\"enabled\":false,"
        "\"permission_granted\":false,\"precision\":\"coarse\","
        "\"upload_interval_seconds\":900,\"retention_days\":7,"
        "\"geofence_radius_meters\":500}}"
    );
    wearable_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    wearable_sync_host_run_pending_task();
    assert(wearable_sync_host_request_count() == 1);
    assert(wearable_sync_host_last_delay_ticks() == 30000u);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_wearable_runtime_and_connectivity();
    test_prepare_propagates_identity_and_base_url_failures();
    test_policy_location_and_sos_close_loop();
    test_policy_rejects_missing_fields_and_invalid_values();
    test_revoked_policy_stops_reporting();
    test_policy_store_failure_is_propagated();
    test_upload_interval_controls_location_reports();
    test_sos_failure_is_not_hidden_by_location_success();
    test_unauthorized_clears_session();
    test_transport_and_request_setup_failures_are_clean();
    test_no_provider_or_invalid_fix_skips_uploads();
    test_initialize_requires_location_and_is_idempotent();
    test_worker_uses_retry_and_regular_intervals();
    return 0;
}
