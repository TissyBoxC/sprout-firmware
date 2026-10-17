#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "volume_control.h"

#ifdef __cplusplus
extern "C" {
#endif

void parent_policy_host_reset(void);
void parent_policy_host_reset_store(void);

void parent_policy_host_set_session_token(const char *token);
void parent_policy_host_set_device_id(const char *device_id);
void parent_policy_host_set_base_url(const char *base_url);
void parent_policy_host_set_cloud_auth_result(esp_err_t result);
void parent_policy_host_set_copy_session_result(esp_err_t result);
void parent_policy_host_set_identity_result(esp_err_t result);
void parent_policy_host_set_base_url_result(esp_err_t result);
void parent_policy_host_set_volume_result(volume_control_error_t result);
void parent_policy_host_set_transport_security_result(esp_err_t result);
void parent_policy_host_set_http_response(int status_code, const char *body);
void parent_policy_host_set_perform_result(esp_err_t result);
void parent_policy_host_set_client_init_fails(bool fails);
void parent_policy_host_set_store_ready(bool ready);
void parent_policy_host_set_store_fail_on_key(const char *key);
void parent_policy_host_set_store_fail_on_erase(const char *key);
void parent_policy_host_set_time_seconds(int64_t seconds);

int parent_policy_host_unauthorized_calls(void);
int parent_policy_host_revoked_calls(void);
int parent_policy_host_request_count(void);
int parent_policy_host_volume_calls(void);
uint8_t parent_policy_host_last_volume_percent(void);
int parent_policy_host_transport_security_calls(void);

const char *parent_policy_host_request_url(size_t index);
const char *parent_policy_host_request_authorization(size_t index);
esp_http_client_method_t parent_policy_host_request_method(size_t index);

bool parent_policy_host_store_has_key(const char *key);
const char *parent_policy_host_store_get(const char *key);

void parent_policy_host_reset_module(void);
void parent_policy_host_set_ready(bool ready);
bool parent_policy_host_is_ready(void);
void parent_policy_host_set_stop(bool stop);

#ifdef __cplusplus
}
#endif
