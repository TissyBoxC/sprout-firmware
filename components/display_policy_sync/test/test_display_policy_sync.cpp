#include "display_policy_sync.h"

#include <cassert>
#include <cstring>

#include "display_policy.h"
#include "display_policy_sync_host.h"
#include "esp_http_client.h"
#include "network_manager.h"

namespace {

const char *policy_body() {
    return "{\"data\":{\"brightness_percent\":200,"
           "\"color_temperature_kelvin\":5000,"
           "\"min_view_distance_cm\":45,\"daily_limit_minutes\":90,"
           "\"break_interval_minutes\":20,\"dim_after_seconds\":30,"
           "\"auto_brightness\":true,\"night_light\":true,"
           "\"distance_reminder\":true,\"revision\":7}}";
}

void reset_all() {
    display_policy_sync_host_reset();
    assert(display_policy_init() == ESP_OK);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        display_policy_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "display_policy_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_live_prerequisites() {
    reset_all();
    display_policy_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(display_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(display_policy_sync_host_request_count() == 0);

    reset_all();
    display_policy_sync_host_set_time_synchronized(false);
    assert(display_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(display_policy_sync_host_request_count() == 0);

    reset_all();
    display_policy_sync_host_set_registered(false);
    assert(display_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(display_policy_sync_host_request_count() == 0);
}

void test_success_fetches_clamps_and_persists_policy() {
    reset_all();
    display_policy_sync_host_set_body(200, policy_body());
    assert(display_policy_sync_now() == ESP_OK);
    assert(display_policy_sync_host_request_count() == 1);
    assert(
        std::strcmp(
            display_policy_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "display/policy"
        ) == 0
    );
    assert(
        std::strcmp(
            display_policy_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(
        display_policy_sync_host_request_method(0) == HTTP_METHOD_GET
    );
    assert(display_policy_sync_host_transport_calls() == 1);
    assert(display_policy_sync_host_store_calls() == 1);
    assert(display_policy_sync_host_has_stored_policy());
    assert(display_policy_sync_host_stored_revision() == 7u);

    const display_policy_t applied = display_policy_get();
    assert(applied.brightness_percent == DISPLAY_POLICY_MAX_BRIGHTNESS);
    assert(applied.color_temperature_kelvin == 5000u);
    assert(applied.daily_limit_minutes == 90u);
    assert(applied.auto_brightness == 1u);
    assert(applied.night_light == 1u);
    assert(applied.distance_reminder == 1u);
    assert(applied.revision == 7u);
}

void test_authentication_failure_clears_session() {
    reset_all();
    display_policy_sync_host_set_body(401, "{}");
    assert(display_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(display_policy_sync_host_clear_session_calls() == 1);

    reset_all();
    display_policy_sync_host_set_body(403, "{}");
    assert(display_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(display_policy_sync_host_clear_session_calls() == 1);
}

void test_transport_and_server_failures_are_distinct() {
    reset_all();
    display_policy_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(display_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(display_policy_sync_host_store_calls() == 0);

    reset_all();
    display_policy_sync_host_set_body(503, "{}");
    assert(display_policy_sync_now() == ESP_FAIL);
    assert(display_policy_sync_host_store_calls() == 0);
}

void test_payload_and_persistence_failures_are_propagated() {
    reset_all();
    display_policy_sync_host_set_body(200, "{\"data\":null}");
    assert(display_policy_sync_now() == ESP_ERR_INVALID_ARG);

    reset_all();
    display_policy_sync_host_set_body(200, policy_body());
    display_policy_sync_host_set_store_result(ESP_ERR_TIMEOUT);
    assert(display_policy_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    display_policy_sync_host_set_body(200, policy_body());
    display_policy_sync_host_set_transport_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(display_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(display_policy_sync_host_request_count() == 0);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_live_prerequisites();
    test_success_fetches_clamps_and_persists_policy();
    test_authentication_failure_clears_session();
    test_transport_and_server_failures_are_distinct();
    test_payload_and_persistence_failures_are_propagated();
    return 0;
}
