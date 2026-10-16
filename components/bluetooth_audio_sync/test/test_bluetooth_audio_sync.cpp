#include "bluetooth_audio_sync.h"

#include <cassert>
#include <cstring>

#include "bluetooth_audio.h"
#include "bluetooth_audio_sync_host.h"
#include "esp_http_client.h"
#include "transport_security.h"

namespace {

void reset_all() {
    bluetooth_audio_sync_host_reset();
    bluetooth_audio_sync_host_reset_module();
    bluetooth_audio_sync_host_reset_audio_state();
    assert(bluetooth_audio_init() == ESP_OK);
}

void set_single_response(int status_code, const char *body) {
    const int statuses[] = {status_code};
    const char *bodies[] = {body};
    bluetooth_audio_sync_host_set_http_sequence(statuses, bodies, 1);
}

void set_policy_and_status_response(
    int policy_status,
    const char *policy_body,
    int status_status,
    const char *status_body
) {
    const int statuses[] = {policy_status, status_status};
    const char *bodies[] = {policy_body, status_body};
    bluetooth_audio_sync_host_set_http_sequence(statuses, bodies, 2);
}

const char *enabled_policy =
    "{\"data\":{\"enabled\":true,\"speaker_mode_allowed\":true,"
    "\"pairing_allowed\":true,\"auto_reconnect\":true,"
    "\"preferred_codec\":\"aac\",\"max_volume_percent\":40,"
    "\"display_name\":\"Kitchen\",\"revision\":12}}";

void test_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        bluetooth_audio_sync_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "bluetooth_audio_sync") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_now_requires_initialized_audio_store() {
    bluetooth_audio_sync_host_reset();
    bluetooth_audio_sync_host_reset_module();
    bluetooth_audio_sync_host_reset_audio_state();

    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);
    assert(bluetooth_audio_sync_host_request_count() == 0);
}

void test_now_requires_trusted_runtime_state() {
    reset_all();

    bluetooth_audio_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);
    assert(bluetooth_audio_sync_host_request_count() == 0);

    bluetooth_audio_sync_host_set_network_state(
        NETWORK_MANAGER_STATE_CONNECTED
    );
    bluetooth_audio_sync_host_set_time_synchronized(false);
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);
    assert(bluetooth_audio_sync_host_request_count() == 0);

    bluetooth_audio_sync_host_set_time_synchronized(true);
    bluetooth_audio_sync_host_set_registered(false);
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);
    assert(bluetooth_audio_sync_host_request_count() == 0);
}

void test_now_rejects_unusable_identity_and_base_url() {
    reset_all();
    bluetooth_audio_sync_host_set_identity_result(ESP_ERR_INVALID_SIZE);
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);
    assert(bluetooth_audio_sync_host_request_count() == 0);

    reset_all();
    bluetooth_audio_sync_host_set_base_url_result(ESP_ERR_INVALID_SIZE);
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);
    assert(bluetooth_audio_sync_host_request_count() == 0);

    reset_all();
    bluetooth_audio_sync_host_set_base_url("http://api.clarkhub.cn");
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);
    assert(bluetooth_audio_sync_host_request_count() == 0);

    reset_all();
    bluetooth_audio_sync_host_set_copy_session_result(ESP_ERR_INVALID_SIZE);
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);
    assert(bluetooth_audio_sync_host_request_count() == 0);
}

void test_successful_fetch_applies_policy_and_reports_status() {
    reset_all();
    set_policy_and_status_response(200, enabled_policy, 201, "");

    assert(bluetooth_audio_sync_now() == ESP_OK);
    assert(bluetooth_audio_sync_host_request_count() == 2);
    assert(
        strcmp(
            bluetooth_audio_sync_host_request_url(0),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "bluetooth-audio/policy"
        ) == 0
    );
    assert(
        strcmp(
            bluetooth_audio_sync_host_request_authorization(0),
            "Bearer session-1"
        ) == 0
    );
    assert(bluetooth_audio_sync_host_request_method(0) == HTTP_METHOD_GET);
    assert(
        strcmp(
            bluetooth_audio_sync_host_request_url(1),
            "https://api.clarkhub.cn/api/v1/devices/device-0001/"
            "bluetooth-audio/status"
        ) == 0
    );
    assert(bluetooth_audio_sync_host_request_method(1) == HTTP_METHOD_POST);
    assert(
        strcmp(
            bluetooth_audio_sync_host_request_body(1),
            "{\"bluetooth_supported\":true,\"speaker_mode\":false,"
            "\"adapter_state\":\"idle\",\"paired_device_count\":0,"
            "\"connected_device_count\":0,\"active_codec\":\"unknown\","
            "\"volume_percent\":0,\"last_disconnect_reason\":\"none\","
            "\"policy_revision\":12}"
        ) == 0
    );

    const bluetooth_policy_t policy = bluetooth_audio_get();
    assert(policy.enabled == 1u);
    assert(policy.speaker_mode_allowed == 1u);
    assert(policy.pairing_allowed == 1u);
    assert(policy.auto_reconnect == 1u);
    assert(policy.preferred_codec == (uint8_t)BLUETOOTH_CODEC_AAC);
    assert(policy.max_volume_percent == 40u);
    assert(strcmp(policy.display_name, "Kitchen") == 0);
    assert(policy.revision == 12u);
    assert(bluetooth_audio_adapter_state() == BLUETOOTH_ADAPTER_IDLE);
}

void test_invalid_policy_leaves_local_policy_untouched() {
    reset_all();
    bluetooth_policy_t original = bluetooth_audio_get();
    original.enabled = 1u;
    original.pairing_allowed = 1u;
    original.revision = 3u;
    assert(bluetooth_audio_set(&original) == ESP_OK);

    set_single_response(
        200,
        "{\"data\":{\"enabled\":true,\"preferred_codec\":\"aptx\"}}"
    );
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_ARG);
    assert(bluetooth_audio_sync_host_request_count() == 1);
    assert(bluetooth_audio_get().revision == 3u);
    assert(bluetooth_audio_get().preferred_codec ==
           original.preferred_codec);

    set_single_response(200, "{\"data\":{}}");
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_ARG);
    assert(bluetooth_audio_get().revision == 3u);

    set_single_response(200, "{not-json");
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_ARG);
    assert(bluetooth_audio_get().revision == 3u);
}

void test_http_and_transport_failures_leave_policy_untouched() {
    reset_all();
    bluetooth_policy_t original = bluetooth_audio_get();
    original.enabled = 1u;
    original.revision = 5u;
    assert(bluetooth_audio_set(&original) == ESP_OK);

    set_single_response(500, enabled_policy);
    assert(bluetooth_audio_sync_now() == ESP_FAIL);
    assert(bluetooth_audio_get().revision == 5u);

    reset_all();
    bluetooth_audio_sync_host_set_client_init_fails(true);
    assert(bluetooth_audio_sync_now() == ESP_ERR_NO_MEM);

    reset_all();
    bluetooth_audio_sync_host_set_perform_result_at(0, ESP_ERR_TIMEOUT);
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);

    reset_all();
    bluetooth_audio_sync_host_set_transport_security_result(
        TRANSPORT_SECURITY_ERR_CLEARTEXT
    );
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_ARG);
    assert(bluetooth_audio_sync_host_request_count() == 0);
}

void test_unauthorized_policy_fetch_clears_session() {
    reset_all();
    set_single_response(401, "{}");
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);
    assert(bluetooth_audio_sync_host_clear_session_calls() == 1);

    reset_all();
    set_single_response(403, "{}");
    assert(bluetooth_audio_sync_now() == ESP_ERR_INVALID_STATE);
    assert(bluetooth_audio_sync_host_clear_session_calls() == 1);
}

void test_status_report_failure_is_best_effort() {
    reset_all();
    set_policy_and_status_response(200, enabled_policy, 500, "{}");
    assert(bluetooth_audio_sync_now() == ESP_OK);
    assert(bluetooth_audio_sync_host_request_count() == 2);
    assert(bluetooth_audio_get().revision == 12u);

    reset_all();
    set_policy_and_status_response(200, enabled_policy, 500, "{}");
    bluetooth_audio_sync_host_set_perform_result_at(1, ESP_ERR_TIMEOUT);
    assert(bluetooth_audio_sync_now() == ESP_OK);
    assert(bluetooth_audio_sync_host_request_count() == 2);
    assert(bluetooth_audio_get().revision == 12u);
}

void test_unauthorized_status_report_clears_session() {
    reset_all();
    set_policy_and_status_response(200, enabled_policy, 401, "{}");
    assert(bluetooth_audio_sync_now() == ESP_OK);
    assert(bluetooth_audio_sync_host_clear_session_calls() == 1);
    assert(bluetooth_audio_get().revision == 12u);

    reset_all();
    set_policy_and_status_response(200, enabled_policy, 403, "{}");
    assert(bluetooth_audio_sync_now() == ESP_OK);
    assert(bluetooth_audio_sync_host_clear_session_calls() == 1);
    assert(bluetooth_audio_get().revision == 12u);
}

void test_initialize_requires_audio_store_and_is_idempotent() {
    bluetooth_audio_sync_host_reset();
    bluetooth_audio_sync_host_reset_module();
    bluetooth_audio_sync_host_reset_audio_state();

    const module_descriptor_t *descriptor =
        bluetooth_audio_sync_module_descriptor();
    assert(descriptor->initialize() == ESP_ERR_INVALID_STATE);
    assert(!bluetooth_audio_sync_host_is_ready());
    assert(bluetooth_audio_sync_host_task_create_calls() == 0);

    assert(bluetooth_audio_init() == ESP_OK);
    bluetooth_audio_sync_host_set_task_create_fails(true);
    assert(descriptor->initialize() == ESP_ERR_NO_MEM);
    assert(!bluetooth_audio_sync_host_is_ready());

    bluetooth_audio_sync_host_set_task_create_fails(false);
    assert(descriptor->initialize() == ESP_OK);
    assert(bluetooth_audio_sync_host_is_ready());
    assert(bluetooth_audio_sync_host_task_create_calls() == 2);
    assert(descriptor->initialize() == ESP_OK);
    assert(bluetooth_audio_sync_host_task_create_calls() == 2);
}

void test_worker_retries_offline_and_stops_cleanly() {
    reset_all();
    const module_descriptor_t *descriptor =
        bluetooth_audio_sync_module_descriptor();
    bluetooth_audio_sync_host_set_network_state(NETWORK_MANAGER_STATE_IDLE);
    bluetooth_audio_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);
    assert(bluetooth_audio_sync_host_task_pending());

    bluetooth_audio_sync_host_run_pending_task();
    assert(bluetooth_audio_sync_host_request_count() == 0);
    assert(bluetooth_audio_sync_host_delay_calls() == 1);
    assert(bluetooth_audio_sync_host_task_delete_calls() == 1);
    assert(!bluetooth_audio_sync_host_task_pending());
}

void test_worker_syncs_then_stops_cleanly() {
    reset_all();
    const module_descriptor_t *descriptor =
        bluetooth_audio_sync_module_descriptor();
    set_policy_and_status_response(200, enabled_policy, 200, "");
    bluetooth_audio_sync_host_set_stop_after_delays(0);
    assert(descriptor->initialize() == ESP_OK);

    bluetooth_audio_sync_host_run_pending_task();
    assert(bluetooth_audio_sync_host_request_count() == 2);
    assert(bluetooth_audio_sync_host_delay_calls() == 1);
    assert(bluetooth_audio_sync_host_task_delete_calls() == 1);
}

}  // namespace

int main() {
    test_descriptor_is_stable();
    test_now_requires_initialized_audio_store();
    test_now_requires_trusted_runtime_state();
    test_now_rejects_unusable_identity_and_base_url();
    test_successful_fetch_applies_policy_and_reports_status();
    test_invalid_policy_leaves_local_policy_untouched();
    test_http_and_transport_failures_leave_policy_untouched();
    test_unauthorized_policy_fetch_clears_session();
    test_status_report_failure_is_best_effort();
    test_unauthorized_status_report_clears_session();
    test_initialize_requires_audio_store_and_is_idempotent();
    test_worker_retries_offline_and_stops_cleanly();
    test_worker_syncs_then_stops_cleanly();
    return 0;
}
