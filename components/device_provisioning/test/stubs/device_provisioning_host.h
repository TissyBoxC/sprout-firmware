#pragma once

#include <stddef.h>
#include <stdint.h>

#include "device_provisioning.h"
#include "esp_err.h"
#include "network_provisioning/manager.h"

#ifdef __cplusplus
extern "C" {
#endif

void device_provisioning_host_reset_state(void);
void device_provisioning_host_reset_event_callback(void);
void device_provisioning_host_install_event_callback(void);
device_provisioning_event_t device_provisioning_host_last_callback_event(void);
void device_provisioning_host_handle_event(
    network_prov_cb_event_t event,
    void *event_data
);
esp_err_t device_provisioning_host_custom_data(
    uint8_t **output,
    ssize_t *output_length
);
void device_provisioning_host_run_work_task(void);
void device_provisioning_host_step_work_task(void);

void device_provisioning_host_reset(void);
void device_provisioning_host_set_mac(const uint8_t mac[6]);
void device_provisioning_host_set_binding_refresh_result(esp_err_t result);
void device_provisioning_host_set_bound(bool bound);
void device_provisioning_host_set_registered(bool registered);
void device_provisioning_host_set_register_result(esp_err_t result);
void device_provisioning_host_set_authenticate_result(esp_err_t result);
void device_provisioning_host_set_create_token_result(esp_err_t result);
void device_provisioning_host_set_check_binding_result(esp_err_t result);
void device_provisioning_host_set_store_credentials_result(esp_err_t result);
void device_provisioning_host_set_reconnect_result(esp_err_t result);
void device_provisioning_host_set_random_sequence(const uint8_t *values, size_t size);
void device_provisioning_host_set_srp_result(esp_err_t result);
void device_provisioning_host_set_srp_salt_leading_byte(uint8_t value);
void device_provisioning_host_set_srp_verifier_size(int size);
void device_provisioning_host_set_config_get_blob_result(esp_err_t result);
void device_provisioning_host_set_config_get_string_result(esp_err_t result);
void device_provisioning_host_clear_config_read_failures(void);
void device_provisioning_host_set_config_set_blob_result(esp_err_t result);
void device_provisioning_host_set_config_set_string_result(esp_err_t result);
void device_provisioning_host_set_task_create_result(int result);
void device_provisioning_host_set_manager_init_result(esp_err_t result);
void device_provisioning_host_set_endpoint_create_result(esp_err_t result);
void device_provisioning_host_set_disable_auto_stop_result(esp_err_t result);
void device_provisioning_host_set_start_result(esp_err_t result);
void device_provisioning_host_set_endpoint_register_result(esp_err_t result);
void device_provisioning_host_set_deinit_result(esp_err_t result);

const char *device_provisioning_host_pending_ssid(void);
const char *device_provisioning_host_pending_password(void);
const char *device_provisioning_host_service_name(void);
const char *device_provisioning_host_setup_uri(void);
const char *device_provisioning_host_binding_uri(void);
const char *device_provisioning_host_salt(size_t *size_out);
const char *device_provisioning_host_verifier(size_t *size_out);
const char *device_provisioning_host_proof_of_possession(void);

int device_provisioning_host_srp_calls(void);
int device_provisioning_host_config_get_blob_calls(void);
int device_provisioning_host_config_get_string_calls(void);
int device_provisioning_host_config_set_blob_calls(void);
int device_provisioning_host_config_set_string_calls(void);
int device_provisioning_host_config_erase_calls(void);
int device_provisioning_host_register_calls(void);
int device_provisioning_host_authenticate_calls(void);
int device_provisioning_host_create_token_calls(void);
int device_provisioning_host_check_binding_calls(void);
int device_provisioning_host_store_credentials_calls(void);
int device_provisioning_host_reconnect_calls(void);
int device_provisioning_host_manager_init_calls(void);
int device_provisioning_host_endpoint_create_calls(void);
int device_provisioning_host_disable_auto_stop_calls(void);
int device_provisioning_host_start_calls(void);
int device_provisioning_host_endpoint_register_calls(void);
int device_provisioning_host_stop_calls(void);
int device_provisioning_host_deinit_calls(void);
int device_provisioning_host_wait_calls(void);
int device_provisioning_host_task_create_calls(void);
int device_provisioning_host_task_delete_calls(void);
int device_provisioning_host_delay_calls(void);

bool device_provisioning_host_task_pending(void);
bool device_provisioning_host_last_started_security2(void);
bool device_provisioning_host_last_wifi_configured(void);
bool device_provisioning_host_last_wifi_failed(void);
bool device_provisioning_host_last_should_prepare_binding(void);
bool device_provisioning_host_should_prepare_binding(void);
bool device_provisioning_host_event_callback_present(void);
const char *device_provisioning_host_last_detail_code(void);
int device_provisioning_host_last_event(void);
int device_provisioning_host_event_callback_calls(void);
const char *device_provisioning_host_last_endpoint(void);
const char *device_provisioning_host_registered_token(void);
void device_provisioning_host_reset_event_callback_observed(void);
void device_provisioning_host_receive_event(
    network_prov_cb_event_t event,
    void *event_data
);

#ifdef __cplusplus
}
#endif
