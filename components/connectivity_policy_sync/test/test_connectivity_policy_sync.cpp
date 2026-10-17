#include "connectivity_policy_sync.h"

#include <cassert>
#include <cstring>

#include "connectivity_policy_sync_core.h"
#include "connectivity_policy_sync_host.h"
#include "esp_http_client.h"
#include "network_manager.h"
#include "transport_security.h"

namespace {

const char *policy_body() {
    return "{\"data\":{\"schema_version\":1,\"enabled\":true,"
           "\"roaming_allowed\":true,\"remote_wake_enabled\":false,"
           "\"metered_upload_limit\":true,\"preference\":\"cellular_first\","
           "\"revision\":7}}";
}

void reset_all() {
    connectivity_policy_sync_host_reset();
    assert(
        connectivity_policy_sync_module_descriptor()->initialize() == ESP_OK
    );
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        connectivity_policy_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "connectivity_policy_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_live_prerequisites() {
    reset_all();
    connectivity_policy_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_IDLE
    );
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(connectivity_policy_sync_host_request_count() == 0);

    reset_all();
    connectivity_policy_sync_host_set_time_synchronized(false);
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(connectivity_policy_sync_host_request_count() == 0);

    reset_all();
    connectivity_policy_sync_host_set_registered(false);
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(connectivity_policy_sync_host_request_count() == 0);
}

void test_success_fetches_and_persists_policy() {
    reset_all();
    connectivity_policy_sync_host_set_body(200, policy_body());
    assert(connectivity_policy_sync_now() == ESP_OK);
    assert(connectivity_policy_sync_host_request_count() == 1);
    assert(
        std::strcmp(
            connectivity_policy_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "connectivity/policy"
        ) == 0
    );
    assert(
        connectivity_policy_sync_host_request_method(0) == HTTP_METHOD_GET
    );
    assert(
        std::strcmp(
            connectivity_policy_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(connectivity_policy_sync_host_transport_calls() == 1);
    assert(connectivity_policy_sync_host_store_calls() == 1);
    assert(connectivity_policy_sync_host_has_stored_policy());
    assert(connectivity_policy_sync_host_stored_revision() == 7u);
    assert(connectivity_policy_applied_revision() == 7u);
}

void test_authentication_failure_clears_session() {
    reset_all();
    connectivity_policy_sync_host_set_body(401, "{}");
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(connectivity_policy_sync_host_clear_session_calls() == 1);

    reset_all();
    connectivity_policy_sync_host_set_body(403, "{}");
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(connectivity_policy_sync_host_clear_session_calls() == 1);
}

void test_transport_server_and_payload_failures_do_not_apply() {
    reset_all();
    connectivity_policy_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(connectivity_policy_sync_host_store_calls() == 0);

    reset_all();
    connectivity_policy_sync_host_set_body(503, "{}");
    assert(connectivity_policy_sync_now() == ESP_FAIL);
    assert(connectivity_policy_sync_host_store_calls() == 0);

    reset_all();
    connectivity_policy_sync_host_set_body(200, "{\"data\":null}");
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(connectivity_policy_sync_host_store_calls() == 0);

    reset_all();
    connectivity_policy_sync_host_set_body(200, "not-json");
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(connectivity_policy_sync_host_store_calls() == 0);

    reset_all();
    connectivity_policy_sync_host_set_body(200, "{\"data\":{}}");
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(connectivity_policy_sync_host_store_calls() == 0);

    reset_all();
    connectivity_policy_sync_host_set_body(
        200,
        "{\"data\":{\"enabled\":\"yes\",\"preference\":1,\"revision\":7}}"
    );
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(connectivity_policy_sync_host_store_calls() == 0);
}

void test_persistence_failure_is_propagated() {
    reset_all();
    connectivity_policy_sync_host_set_body(200, policy_body());
    connectivity_policy_sync_host_set_store_result(ESP_ERR_TIMEOUT);
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(connectivity_policy_sync_host_store_calls() == 1);
}

void test_cleartext_url_is_rejected_before_network_io() {
    reset_all();
    connectivity_policy_sync_host_set_transport_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    connectivity_policy_sync_host_set_body(200, policy_body());
    assert(connectivity_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(connectivity_policy_sync_host_request_count() == 0);
    assert(connectivity_policy_sync_host_store_calls() == 0);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_live_prerequisites();
    test_success_fetches_and_persists_policy();
    test_authentication_failure_clears_session();
    test_transport_server_and_payload_failures_do_not_apply();
    test_persistence_failure_is_propagated();
    test_cleartext_url_is_rejected_before_network_io();
    return 0;
}
