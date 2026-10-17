#include "power_policy_sync.h"

#include <cassert>
#include <cstring>

#include "battery_power.h"
#include "esp_http_client.h"
#include "network_manager.h"
#include "power_policy_sync_host.h"
#include "transport_security.h"

namespace {

const char *policy_body() {
    return "{\"data\":{\"enabled\":true,\"low_power_enabled\":false,"
           "\"deep_sleep_enabled\":true,\"charge_priority\":false,"
           "\"low_battery_warn_percent\":30,\"low_power_enter_percent\":10,"
           "\"sleep_after_seconds\":1200,\"revision\":7}}";
}

void reset_all() {
    power_policy_sync_host_reset();
    assert(battery_init() == ESP_OK);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        power_policy_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "power_policy_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_live_prerequisites() {
    reset_all();
    power_policy_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(power_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(power_policy_sync_host_request_count() == 0);

    reset_all();
    power_policy_sync_host_set_time_synchronized(false);
    assert(power_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(power_policy_sync_host_request_count() == 0);

    reset_all();
    power_policy_sync_host_set_registered(false);
    assert(power_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(power_policy_sync_host_request_count() == 0);
}

void test_success_fetches_and_persists_policy() {
    reset_all();
    power_policy_sync_host_set_body(200, policy_body());
    assert(power_policy_sync_now() == ESP_OK);
    assert(power_policy_sync_host_request_count() == 1);
    assert(
        std::strcmp(
            power_policy_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/power/policy"
        ) == 0
    );
    assert(power_policy_sync_host_request_method(0) == HTTP_METHOD_GET);
    assert(
        std::strcmp(
            power_policy_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(power_policy_sync_host_transport_calls() == 1);
    assert(power_policy_sync_host_store_calls() == 1);
    assert(power_policy_sync_host_has_stored_policy());
    assert(power_policy_sync_host_stored_revision() == 7u);

    const battery_policy_t applied = battery_get();
    assert(applied.enabled == 1u);
    assert(applied.low_power_enabled == 0u);
    assert(applied.deep_sleep_enabled == 1u);
    assert(applied.charge_priority == 0u);
    assert(applied.low_battery_warn_percent == 30u);
    assert(applied.low_power_enter_percent == 10u);
    assert(applied.sleep_after_seconds == 1200u);
    assert(applied.revision == 7u);
}

void test_authentication_and_transport_failures_do_not_apply() {
    reset_all();
    power_policy_sync_host_set_body(401, "{}");
    assert(power_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(power_policy_sync_host_clear_session_calls() == 1);

    reset_all();
    power_policy_sync_host_set_body(403, "{}");
    assert(power_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(power_policy_sync_host_clear_session_calls() == 1);

    reset_all();
    power_policy_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(power_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(power_policy_sync_host_store_calls() == 0);

    reset_all();
    power_policy_sync_host_set_body(503, "{}");
    assert(power_policy_sync_now() == ESP_FAIL);
    assert(power_policy_sync_host_store_calls() == 0);
}

void test_invalid_payloads_do_not_apply() {
    reset_all();
    power_policy_sync_host_set_body(200, "not-json");
    assert(power_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(power_policy_sync_host_store_calls() == 0);

    reset_all();
    power_policy_sync_host_set_body(200, "{\"data\":{}}");
    assert(power_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(power_policy_sync_host_store_calls() == 0);

    reset_all();
    power_policy_sync_host_set_body(
        200,
        "{\"data\":{\"enabled\":\"yes\",\"low_battery_warn_percent\":30,"
        "\"low_power_enter_percent\":10,\"sleep_after_seconds\":1200,"
        "\"revision\":7}}"
    );
    assert(power_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(power_policy_sync_host_store_calls() == 0);
}

void test_persistence_and_transport_security_failures_are_propagated() {
    reset_all();
    power_policy_sync_host_set_body(200, policy_body());
    power_policy_sync_host_set_store_result(ESP_ERR_TIMEOUT);
    assert(power_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(power_policy_sync_host_store_calls() == 1);

    reset_all();
    power_policy_sync_host_set_transport_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    power_policy_sync_host_set_body(200, policy_body());
    assert(power_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(power_policy_sync_host_request_count() == 0);
    assert(power_policy_sync_host_store_calls() == 0);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_live_prerequisites();
    test_success_fetches_and_persists_policy();
    test_authentication_and_transport_failures_do_not_apply();
    test_invalid_payloads_do_not_apply();
    test_persistence_and_transport_security_failures_are_propagated();
    return 0;
}
