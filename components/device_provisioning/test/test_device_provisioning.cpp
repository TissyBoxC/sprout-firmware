#include "device_provisioning.h"

#include "device_provisioning_host.h"
#include "esp_wifi.h"
#include "provisioning_payload.h"

#include <assert.h>
#include <string.h>

namespace {

void reset_all() {
    device_provisioning_host_reset_state();
    device_provisioning_host_reset();
}

void reset_runtime() {
    device_provisioning_host_reset_state();
    device_provisioning_host_clear_config_read_failures();
}

void test_module_descriptor_is_stable() {
    const module_descriptor_t *descriptor =
        device_provisioning_module_descriptor();
    assert(descriptor != nullptr);
    assert(strcmp(descriptor->module_name, "device_provisioning") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != nullptr);
    assert(descriptor->shutdown == nullptr);
}

void test_first_boot_generates_credentials_and_setup_uri() {
    reset_all();
    const uint8_t mac[6] = {0x10, 0x20, 0x30, 0xAA, 0xBB, 0xCC};
    device_provisioning_host_set_mac(mac);

    assert(device_provisioning_init() == ESP_OK);
    assert(device_provisioning_host_srp_calls() == 1);
    assert(device_provisioning_host_config_set_blob_calls() == 2);
    assert(device_provisioning_host_config_set_string_calls() == 1);
    size_t salt_size = 0;
    size_t verifier_size = 0;
    const char *const salt = device_provisioning_host_salt(&salt_size);
    const char *const verifier =
        device_provisioning_host_verifier(&verifier_size);
    assert(salt_size == 16);
    assert(verifier_size == 128);
    assert(salt != nullptr);
    assert(verifier != nullptr);
    assert((static_cast<unsigned char>(salt[0]) & 0x80) != 0);
    assert(strlen(device_provisioning_host_proof_of_possession()) == 8);

    assert(strcmp(
               device_provisioning_host_service_name(),
               "SPROUT-AABBCC"
           ) == 0);
    assert(strncmp(
               device_provisioning_host_setup_uri(),
               "sprout://setup?service=SPROUT-AABBCC&pop=",
               strlen("sprout://setup?service=SPROUT-AABBCC&pop=")
           ) == 0);
    assert(strstr(device_provisioning_host_setup_uri(), "token=") == nullptr);
    assert(device_provisioning_host_last_started_security2());
    assert(strcmp(device_provisioning_host_last_endpoint(), "custom-data") == 0);
    assert(device_provisioning_host_task_pending());

    /* Repeated init must not regenerate or start a second service. */
    assert(device_provisioning_init() == ESP_OK);
    assert(device_provisioning_host_srp_calls() == 1);
    assert(device_provisioning_host_manager_init_calls() == 1);
}

void test_existing_credentials_are_reused() {
    reset_all();
    assert(device_provisioning_init() == ESP_OK);
    assert(device_provisioning_host_srp_calls() == 1);
    const int srp_calls_before_restart =
        device_provisioning_host_srp_calls();

    reset_runtime();
    assert(device_provisioning_init() == ESP_OK);
    assert(
        device_provisioning_host_srp_calls() == srp_calls_before_restart
    );
    assert(device_provisioning_host_config_get_blob_calls() >= 2);
    assert(device_provisioning_host_config_get_string_calls() >= 1);
}

void test_srp_retries_until_high_bit_is_set() {
    reset_all();
    assert(device_provisioning_init() == ESP_OK);
    const int calls = device_provisioning_host_srp_calls();
    assert(calls >= 1);

    reset_all();
    device_provisioning_host_set_srp_salt_leading_byte(0x10);
    assert(device_provisioning_init() == ESP_ERR_INVALID_SIZE);
    assert(device_provisioning_host_srp_calls() == 8);
}

void test_srp_failure_blocks_init() {
    reset_all();
    device_provisioning_host_set_srp_result(ESP_ERR_NO_MEM);
    assert(device_provisioning_init() == ESP_ERR_NO_MEM);
    assert(device_provisioning_host_manager_init_calls() == 0);
}

void test_bound_device_reconnects_without_ble() {
    reset_all();
    device_provisioning_host_set_bound(true);
    assert(device_provisioning_init() == ESP_OK);
    assert(device_provisioning_host_reconnect_calls() == 1);
    assert(device_provisioning_host_manager_init_calls() == 0);
    assert(device_provisioning_host_task_create_calls() == 0);
}

void test_binding_refresh_failure_is_propagated() {
    reset_all();
    device_provisioning_host_set_binding_refresh_result(ESP_ERR_TIMEOUT);
    assert(device_provisioning_init() == ESP_ERR_TIMEOUT);
    assert(device_provisioning_host_manager_init_calls() == 0);
}

void test_custom_data_requires_active_binding_uri() {
    reset_all();
    uint8_t *output = nullptr;
    ssize_t output_length = 0;
    assert(
        device_provisioning_host_custom_data(&output, &output_length) ==
        ESP_ERR_INVALID_STATE
    );
    assert(output_length == 0);
}

void test_custom_data_returns_binding_uri_copy() {
    reset_all();
    assert(device_provisioning_init() == ESP_OK);
    assert(device_provisioning_host_task_pending());

    /* The URI is only published after Wi-Fi credentials are accepted. */
    uint8_t *output = nullptr;
    ssize_t output_length = 0;
    assert(
        device_provisioning_host_custom_data(&output, &output_length) ==
        ESP_ERR_INVALID_STATE
    );

    network_prov_cb_event_t success = NETWORK_PROV_WIFI_CRED_SUCCESS;
    device_provisioning_host_handle_event(success, nullptr);
    assert(device_provisioning_host_should_prepare_binding());
    device_provisioning_host_step_work_task();

    assert(device_provisioning_host_authenticate_calls() == 1);
    assert(device_provisioning_host_create_token_calls() == 1);
    assert(
        device_provisioning_host_custom_data(&output, &output_length) ==
        ESP_OK
    );
    assert(output != nullptr);
    assert(output_length > 0);
    assert(
        strncmp(
            reinterpret_cast<const char *>(output),
            "sprout://device?device_id=device-0001&token=",
            strlen("sprout://device?device_id=device-0001&token=")
        ) == 0
    );
    free(output);
}

void test_work_task_persists_pending_credentials() {
    reset_all();
    assert(device_provisioning_init() == ESP_OK);

    wifi_sta_config_t station = {};
    memcpy(station.ssid, "GuardianWiFi", strlen("GuardianWiFi"));
    memcpy(station.password, "secret-pass", strlen("secret-pass"));
    device_provisioning_host_handle_event(NETWORK_PROV_WIFI_CRED_RECV, &station);
    assert(device_provisioning_host_should_prepare_binding() == false);
    assert(device_provisioning_host_last_wifi_configured() == false);

    device_provisioning_host_handle_event(NETWORK_PROV_WIFI_CRED_SUCCESS, nullptr);
    device_provisioning_host_step_work_task();
    assert(device_provisioning_host_store_credentials_calls() == 1);
    assert(device_provisioning_host_authenticate_calls() == 1);
}

void test_work_task_keeps_credentials_after_store_failure() {
    reset_all();
    device_provisioning_host_set_store_credentials_result(ESP_ERR_TIMEOUT);
    assert(device_provisioning_init() == ESP_OK);

    wifi_sta_config_t station = {};
    memcpy(station.ssid, "GuardianWiFi", strlen("GuardianWiFi"));
    memcpy(station.password, "secret-pass", strlen("secret-pass"));
    device_provisioning_host_handle_event(NETWORK_PROV_WIFI_CRED_RECV, &station);
    device_provisioning_host_handle_event(NETWORK_PROV_WIFI_CRED_SUCCESS, nullptr);
    device_provisioning_host_step_work_task();

    assert(device_provisioning_host_store_credentials_calls() == 1);
    assert(device_provisioning_host_authenticate_calls() == 0);

    device_provisioning_host_set_store_credentials_result(ESP_OK);
    device_provisioning_host_step_work_task();
    assert(device_provisioning_host_store_credentials_calls() == 2);
    assert(device_provisioning_host_authenticate_calls() == 1);
}

void test_invalid_credentials_are_ignored() {
    reset_all();
    assert(device_provisioning_init() == ESP_OK);

    wifi_sta_config_t station = {};
    device_provisioning_host_handle_event(NETWORK_PROV_WIFI_CRED_RECV, &station);
    device_provisioning_host_handle_event(NETWORK_PROV_WIFI_CRED_SUCCESS, nullptr);
    device_provisioning_host_step_work_task();

    assert(device_provisioning_host_store_credentials_calls() == 1);
    assert(device_provisioning_host_authenticate_calls() == 1);
}

void test_events_report_expected_callback_details() {
    reset_all();
    device_provisioning_host_reset_event_callback();
    device_provisioning_host_install_event_callback();

    assert(device_provisioning_init() == ESP_OK);
    device_provisioning_host_handle_event(NETWORK_PROV_START, nullptr);
    assert(device_provisioning_is_active());
    assert(device_provisioning_host_event_callback_calls() == 1);
    assert(
        device_provisioning_host_last_callback_event() ==
        DEVICE_PROVISIONING_EVENT_STARTED
    );
    assert(strcmp(
               device_provisioning_host_last_detail_code(),
               "ble_service_started"
           ) == 0);

    network_prov_wifi_sta_fail_reason_t auth =
        NETWORK_PROV_WIFI_STA_AUTH_ERROR;
    device_provisioning_host_handle_event(NETWORK_PROV_WIFI_CRED_FAIL, &auth);
    assert(device_provisioning_host_event_callback_calls() == 2);
    assert(
        device_provisioning_host_last_callback_event() ==
        DEVICE_PROVISIONING_EVENT_WIFI_FAILED
    );
    assert(strcmp(
               device_provisioning_host_last_detail_code(),
               "authentication_error"
           ) == 0);

    network_prov_wifi_sta_fail_reason_t missing =
        NETWORK_PROV_WIFI_STA_DISCONNECTED;
    device_provisioning_host_handle_event(NETWORK_PROV_WIFI_CRED_FAIL, &missing);
    assert(strcmp(
               device_provisioning_host_last_detail_code(),
               "access_point_not_found"
           ) == 0);

    device_provisioning_host_handle_event(NETWORK_PROV_END, nullptr);
    assert(!device_provisioning_is_active());
}

void test_copy_setup_payload_validates_state_and_buffers() {
    reset_all();
    char payload[PROVISIONING_PAYLOAD_SIZE] = {0};
    assert(
        device_provisioning_copy_setup_payload(payload, sizeof(payload)) ==
        ESP_ERR_INVALID_STATE
    );
    assert(device_provisioning_copy_setup_payload(nullptr, 0) ==
           ESP_ERR_INVALID_ARG);

    assert(device_provisioning_init() == ESP_OK);
    device_provisioning_host_handle_event(NETWORK_PROV_START, nullptr);
    assert(
        device_provisioning_copy_setup_payload(payload, sizeof(payload)) ==
        ESP_OK
    );
    assert(strncmp(payload, "sprout://setup?", 15) == 0);

    char too_small[8] = {0};
    assert(
        device_provisioning_copy_setup_payload(too_small, sizeof(too_small)) ==
        ESP_ERR_INVALID_SIZE
    );
}

void test_service_start_failures_are_cleaned_up() {
    reset_all();
    device_provisioning_host_set_endpoint_create_result(ESP_ERR_NO_MEM);
    assert(device_provisioning_init() == ESP_ERR_NO_MEM);
    assert(device_provisioning_host_deinit_calls() == 1);
    assert(device_provisioning_host_task_create_calls() == 0);

    reset_all();
    device_provisioning_host_set_disable_auto_stop_result(ESP_FAIL);
    assert(device_provisioning_init() == ESP_FAIL);
    assert(device_provisioning_host_deinit_calls() == 1);

    reset_all();
    device_provisioning_host_set_start_result(ESP_ERR_TIMEOUT);
    assert(device_provisioning_init() == ESP_ERR_TIMEOUT);
    assert(device_provisioning_host_deinit_calls() == 1);

    reset_all();
    device_provisioning_host_set_endpoint_register_result(ESP_ERR_INVALID_STATE);
    assert(device_provisioning_init() == ESP_ERR_INVALID_STATE);
    assert(device_provisioning_host_stop_calls() == 1);
    assert(device_provisioning_host_deinit_calls() == 0);

    reset_all();
    device_provisioning_host_set_task_create_result(0);
    assert(device_provisioning_init() == ESP_ERR_NO_MEM);
    assert(device_provisioning_host_stop_calls() == 1);
}

void test_bound_work_task_rotates_credentials_and_cleans_up() {
    reset_all();
    assert(device_provisioning_init() == ESP_OK);
    device_provisioning_host_handle_event(NETWORK_PROV_WIFI_CRED_SUCCESS, nullptr);
    device_provisioning_host_set_bound(true);
    device_provisioning_host_step_work_task();
    assert(device_provisioning_host_config_erase_calls() == 3);
    assert(device_provisioning_host_stop_calls() == 1);
    assert(device_provisioning_host_wait_calls() == 1);
    assert(device_provisioning_host_deinit_calls() == 1);
    assert(device_provisioning_host_reconnect_calls() == 1);
}

void test_work_task_retries_authentication_then_binds() {
    reset_all();
    device_provisioning_host_set_registered(false);
    device_provisioning_host_set_register_result(ESP_OK);
    device_provisioning_host_set_authenticate_result(ESP_ERR_TIMEOUT);
    assert(device_provisioning_init() == ESP_OK);

    device_provisioning_host_handle_event(NETWORK_PROV_WIFI_CRED_SUCCESS, nullptr);
    device_provisioning_host_step_work_task();
    assert(device_provisioning_host_register_calls() == 1);
    assert(device_provisioning_host_authenticate_calls() == 1);
    assert(device_provisioning_host_create_token_calls() == 0);

    device_provisioning_host_set_authenticate_result(ESP_OK);
    device_provisioning_host_step_work_task();
    assert(device_provisioning_host_register_calls() == 2);
    assert(device_provisioning_host_authenticate_calls() == 2);
    assert(device_provisioning_host_create_token_calls() == 1);
}

}  // namespace

int main() {
    test_module_descriptor_is_stable();
    test_first_boot_generates_credentials_and_setup_uri();
    test_existing_credentials_are_reused();
    test_srp_retries_until_high_bit_is_set();
    test_srp_failure_blocks_init();
    test_bound_device_reconnects_without_ble();
    test_binding_refresh_failure_is_propagated();
    test_custom_data_requires_active_binding_uri();
    test_custom_data_returns_binding_uri_copy();
    test_work_task_persists_pending_credentials();
    test_work_task_keeps_credentials_after_store_failure();
    test_invalid_credentials_are_ignored();
    test_events_report_expected_callback_details();
    test_copy_setup_payload_validates_state_and_buffers();
    test_service_start_failures_are_cleaned_up();
    test_bound_work_task_rotates_credentials_and_cleans_up();
    test_work_task_retries_authentication_then_binds();
    return 0;
}
