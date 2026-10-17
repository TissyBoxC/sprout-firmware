#include "motion_policy_sync.h"

#include <cassert>
#include <cstring>

#include "esp_http_client.h"
#include "motion_control.h"
#include "motion_policy_sync_host.h"
#include "network_manager.h"
#include "transport_security.h"

namespace {

const char *policy_body() {
    return "{\"data\":{\"enabled\":true,\"require_stop_ack\":true,"
           "\"allow_locomotion\":false,\"max_speed_percent\":75,"
           "\"max_runtime_ms\":4000,\"stop_grace_ms\":250,\"revision\":9}}";
}

void reset_all() {
    motion_policy_sync_host_reset();
    assert(motion_init() == ESP_OK);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        motion_policy_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "motion_policy_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_runtime_prerequisites() {
    reset_all();
    motion_policy_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(motion_policy_sync_host_request_count() == 0);

    reset_all();
    motion_policy_sync_host_set_time_synchronized(false);
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(motion_policy_sync_host_request_count() == 0);

    reset_all();
    motion_policy_sync_host_set_registered(false);
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(motion_policy_sync_host_request_count() == 0);
}

void test_success_fetches_and_persists_policy() {
    reset_all();
    motion_policy_sync_host_set_body(200, policy_body());
    assert(motion_policy_sync_now() == ESP_OK);
    assert(motion_policy_sync_host_request_count() == 1);
    assert(
        std::strcmp(
            motion_policy_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/motion/policy"
        ) == 0
    );
    assert(motion_policy_sync_host_request_method(0) == HTTP_METHOD_GET);
    assert(
        std::strcmp(
            motion_policy_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(motion_policy_sync_host_transport_calls() == 1);
    assert(motion_policy_sync_host_store_calls() == 1);
    assert(motion_policy_sync_host_has_stored_policy());
    assert(motion_policy_sync_host_stored_revision() == 9u);

    const motion_policy_t applied = motion_get();
    assert(applied.enabled == 1u);
    assert(applied.require_stop_ack == 1u);
    assert(applied.allow_locomotion == 0u);
    assert(applied.max_speed_percent == 75u);
    assert(applied.max_runtime_ms == 4000u);
    assert(applied.stop_grace_ms == 250u);
    assert(applied.revision == 9u);
}

void test_authentication_and_transport_failures_do_not_apply() {
    reset_all();
    motion_policy_sync_host_set_body(401, "{}");
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(motion_policy_sync_host_clear_session_calls() == 1);

    reset_all();
    motion_policy_sync_host_set_body(403, "{}");
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(motion_policy_sync_host_clear_session_calls() == 1);

    reset_all();
    motion_policy_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(motion_policy_sync_host_store_calls() == 0);

    reset_all();
    motion_policy_sync_host_set_body(503, "{}");
    assert(motion_policy_sync_now() == ESP_FAIL);
    assert(motion_policy_sync_host_store_calls() == 0);
}

void test_invalid_payloads_do_not_apply() {
    reset_all();
    motion_policy_sync_host_set_body(200, "not-json");
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(motion_policy_sync_host_store_calls() == 0);

    reset_all();
    motion_policy_sync_host_set_body(200, "{\"data\":{}}");
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(motion_policy_sync_host_store_calls() == 0);

    reset_all();
    motion_policy_sync_host_set_body(
        200,
        "{\"data\":{\"enabled\":\"yes\",\"max_speed_percent\":75,"
        "\"max_runtime_ms\":4000,\"stop_grace_ms\":250,\"revision\":9}}"
    );
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(motion_policy_sync_host_store_calls() == 0);
}

void test_persistence_and_transport_security_failures_are_propagated() {
    reset_all();
    motion_policy_sync_host_set_body(200, policy_body());
    motion_policy_sync_host_set_store_result(ESP_ERR_TIMEOUT);
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(motion_policy_sync_host_store_calls() == 1);

    reset_all();
    motion_policy_sync_host_set_transport_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    motion_policy_sync_host_set_body(200, policy_body());
    assert(motion_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(motion_policy_sync_host_request_count() == 0);
    assert(motion_policy_sync_host_store_calls() == 0);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_runtime_prerequisites();
    test_success_fetches_and_persists_policy();
    test_authentication_and_transport_failures_do_not_apply();
    test_invalid_payloads_do_not_apply();
    test_persistence_and_transport_security_failures_are_propagated();
    return 0;
}
