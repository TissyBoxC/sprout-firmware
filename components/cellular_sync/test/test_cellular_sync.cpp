#include "cellular_sync.h"

#include <cassert>
#include <cstring>

#include "cellular_4g.h"
#include "cellular_sync_host.h"
#include "esp_http_client.h"
#include "network_manager.h"
#include "transport_security.h"

namespace {

const char *policy_body() {
    return "{\"data\":{\"schema_version\":1,\"enabled\":true,"
           "\"roaming_allowed\":true,\"remote_wake_enabled\":true,"
           "\"metered_upload_limit\":true,\"preference\":\"cellular_first\","
           "\"monthly_quota_mb\":5000,\"low_quota_warn_percent\":20,"
           "\"keepalive_seconds\":120,\"policy_revision\":7}}";
}

void reset_all() {
    cellular_sync_host_reset();
    assert(cellular_init() == ESP_OK);
    assert(cellular_sync_register_provider(nullptr) == ESP_OK);
}

void set_sequence(const int *statuses, const char *const *bodies, size_t count) {
    const int defaults[] = {200, 200};
    cellular_sync_host_set_http_sequence(
        statuses != nullptr ? statuses : defaults,
        bodies,
        count
    );
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        cellular_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "cellular_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_live_prerequisites() {
    reset_all();
    cellular_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(cellular_sync_now() == ESP_ERR_INVALID_STATE);
    assert(cellular_sync_host_request_count() == 0);

    reset_all();
    cellular_sync_host_set_time_synchronized(false);
    assert(cellular_sync_now() == ESP_ERR_INVALID_STATE);
    assert(cellular_sync_host_request_count() == 0);

    reset_all();
    cellular_sync_host_set_registered(false);
    assert(cellular_sync_now() == ESP_ERR_INVALID_STATE);
    assert(cellular_sync_host_request_count() == 0);
}

void test_success_applies_policy_and_reports_status() {
    reset_all();
    const int statuses[] = {200, 200};
    const char *bodies[] = {policy_body(), "{}"};
    set_sequence(statuses, bodies, 2);
    assert(cellular_sync_now() == ESP_OK);
    assert(cellular_sync_host_request_count() == 2);
    assert(
        std::strcmp(
            cellular_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "connectivity/policy"
        ) == 0
    );
    assert(
        std::strcmp(
            cellular_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "connectivity/status"
        ) == 0
    );
    assert(
        cellular_sync_host_request_method(0) == HTTP_METHOD_GET
    );
    assert(
        cellular_sync_host_request_method(1) == HTTP_METHOD_POST
    );
    assert(
        std::strcmp(
            cellular_sync_host_request_authorization(1),
            "Bearer session-1"
        ) == 0
    );
    assert(cellular_sync_host_transport_calls() == 2);
    assert(cellular_sync_host_store_calls() == 1);
    assert(cellular_sync_host_has_stored_policy());
    assert(cellular_sync_host_stored_revision() == 7u);

    const cellular_policy_t applied = cellular_get();
    assert(applied.enabled == 1u);
    assert(applied.roaming_allowed == 1u);
    assert(applied.preference == CELLULAR_PREFERENCE_CELLULAR_FIRST);
    assert(applied.monthly_quota_mb == 5000u);
    assert(applied.revision == 7u);
}

void test_status_report_failure_is_propagated_after_apply() {
    reset_all();
    const int statuses[] = {200, 503};
    const char *bodies[] = {policy_body(), "{}"};
    set_sequence(statuses, bodies, 2);
    assert(cellular_sync_now() == ESP_FAIL);
    assert(cellular_get().revision == 7u);
}

void test_policy_failure_skips_status_report() {
    reset_all();
    cellular_sync_host_set_store_result(ESP_ERR_TIMEOUT);
    const char *body = policy_body();
    set_sequence(nullptr, &body, 1);
    assert(cellular_sync_now() == ESP_ERR_INVALID_STATE);
    assert(cellular_sync_host_request_count() == 1);
    assert(cellular_sync_host_store_calls() == 1);
}

void test_authentication_failure_clears_session() {
    reset_all();
    const int statuses[] = {401};
    const char *bodies[] = {"{}"};
    set_sequence(statuses, bodies, 1);
    assert(cellular_sync_now() == ESP_ERR_INVALID_STATE);
    assert(cellular_sync_host_clear_session_calls() == 1);
}

void test_transport_and_payload_failures_do_not_apply() {
    reset_all();
    cellular_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    const char *body = policy_body();
    set_sequence(nullptr, &body, 1);
    assert(cellular_sync_now() == ESP_ERR_INVALID_STATE);
    assert(cellular_sync_host_store_calls() == 0);

    reset_all();
    const int statuses[] = {503};
    set_sequence(statuses, &body, 1);
    assert(cellular_sync_now() == ESP_FAIL);
    assert(cellular_sync_host_store_calls() == 0);

    reset_all();
    const char *invalid = "{\"data\":{\"schema_version\":1}}";
    set_sequence(nullptr, &invalid, 1);
    assert(cellular_sync_now() == ESP_ERR_INVALID_ARG);
    assert(cellular_sync_host_store_calls() == 0);

    reset_all();
    cellular_sync_host_set_transport_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    set_sequence(nullptr, &body, 1);
    assert(cellular_sync_now() == ESP_ERR_INVALID_ARG);
    assert(cellular_sync_host_request_count() == 0);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_live_prerequisites();
    test_success_applies_policy_and_reports_status();
    test_status_report_failure_is_propagated_after_apply();
    test_policy_failure_skips_status_report();
    test_authentication_failure_clears_session();
    test_transport_and_payload_failures_do_not_apply();
    return 0;
}
