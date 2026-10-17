#include "screen_policy_sync.h"

#include <cassert>
#include <cstring>

#include "config_store_test.h"
#include "esp_http_client.h"
#include "network_manager.h"
#include "screen_policy_sync_host.h"
#include "screen_surface.h"
#include "transport_security.h"

namespace {

const char *policy_body() {
    return "{\"data\":{\"default_expression\":\"curious\","
           "\"animation_enabled\":true,\"point_read_enabled\":true,"
           "\"video_enabled\":false,\"video_max_fps\":45,"
           "\"video_max_bitrate_kbps\":4000,\"animation_frame_ms\":120,"
           "\"revision\":9}}";
}

void reset_all() {
    screen_config_store_test_reset();
    screen_policy_sync_host_reset();
    assert(screen_surface_init() == ESP_OK);
}

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        screen_policy_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(std::strcmp(descriptor->module_name, "screen_policy_sync") == 0);
    assert(std::strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_live_prerequisites() {
    reset_all();
    screen_policy_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(screen_policy_sync_host_request_count() == 0);

    reset_all();
    screen_policy_sync_host_set_time_synchronized(false);
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(screen_policy_sync_host_request_count() == 0);

    reset_all();
    screen_policy_sync_host_set_registered(false);
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(screen_policy_sync_host_request_count() == 0);
}

void test_success_fetches_and_persists_policy() {
    reset_all();
    screen_policy_sync_host_set_body(200, policy_body());
    assert(screen_policy_sync_now() == ESP_OK);
    assert(screen_policy_sync_host_request_count() == 1);
    assert(
        std::strcmp(
            screen_policy_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/screen/policy"
        ) == 0
    );
    assert(screen_policy_sync_host_request_method(0) == HTTP_METHOD_GET);
    assert(
        std::strcmp(
            screen_policy_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(screen_policy_sync_host_transport_calls() == 1);

    const screen_policy_t applied = screen_surface_get();
    assert(applied.enabled == 1u);
    assert(applied.default_expression == SCREEN_EXPRESSION_CURIOUS);
    assert(applied.animation_enabled == 1u);
    assert(applied.point_read_enabled == 1u);
    assert(applied.video_enabled == 0u);
    assert(applied.video_max_fps == 45u);
    assert(applied.video_max_bitrate_kbps == 4000u);
    assert(applied.animation_frame_ms == 120u);
    assert(applied.revision == 9u);
}

void test_authentication_and_transport_failures_do_not_apply() {
    reset_all();
    screen_policy_sync_host_set_body(401, "{}");
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(screen_policy_sync_host_clear_session_calls() == 1);

    reset_all();
    screen_policy_sync_host_set_body(403, "{}");
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(screen_policy_sync_host_clear_session_calls() == 1);

    reset_all();
    screen_policy_sync_host_set_perform_result(ESP_ERR_TIMEOUT);
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    screen_policy_sync_host_set_body(503, "{}");
    assert(screen_policy_sync_now() == ESP_FAIL);
}

void test_invalid_payloads_do_not_apply() {
    reset_all();
    screen_policy_sync_host_set_body(200, "not-json");
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_ARG);

    reset_all();
    screen_policy_sync_host_set_body(200, "{\"data\":{}}");
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_ARG);

    reset_all();
    screen_policy_sync_host_set_body(
        200,
        "{\"data\":{\"default_expression\":\"curious\","
        "\"animation_enabled\":\"yes\",\"point_read_enabled\":true,"
        "\"video_enabled\":false,\"video_max_fps\":45,"
        "\"video_max_bitrate_kbps\":4000,\"animation_frame_ms\":120,"
        "\"revision\":9}}"
    );
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_ARG);

    reset_all();
    screen_policy_sync_host_set_body(
        200,
        "{\"data\":{\"default_expression\":\"furious\","
        "\"animation_enabled\":true,\"point_read_enabled\":true,"
        "\"video_enabled\":false,\"video_max_fps\":45,"
        "\"video_max_bitrate_kbps\":4000,\"animation_frame_ms\":120,"
        "\"revision\":9}}"
    );
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_ARG);

    reset_all();
    screen_policy_sync_host_set_body(
        200,
        "{\"data\":{\"default_expression\":\"curious\","
        "\"animation_enabled\":true,\"point_read_enabled\":true,"
        "\"video_enabled\":false,\"video_max_fps\":45,"
        "\"video_max_bitrate_kbps\":4000,\"animation_frame_ms\":120,"
        "\"revision\":-1}}"
    );
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_ARG);
}

void test_persistence_and_transport_security_failures_are_propagated() {
    reset_all();
    screen_policy_sync_host_set_body(200, policy_body());
    screen_config_store_test_set_set_result(ESP_ERR_TIMEOUT);
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    screen_policy_sync_host_set_transport_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    screen_policy_sync_host_set_body(200, policy_body());
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_ARG);
    assert(screen_policy_sync_host_request_count() == 0);

    reset_all();
    screen_policy_sync_host_set_base_url("http://api.clarkhub.cn");
    screen_policy_sync_host_set_body(200, policy_body());
    assert(screen_policy_sync_now() == ESP_ERR_INVALID_STATE);
    assert(screen_policy_sync_host_request_count() == 0);
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
