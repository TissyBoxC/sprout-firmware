#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "wearable_location.h"

#ifdef __cplusplus
extern "C" {
#endif

void wearable_sync_host_reset(void);
void wearable_sync_host_reset_module(void);
void wearable_sync_host_set_stop(bool stop);
bool wearable_sync_host_is_ready(void);

void wearable_sync_host_set_network_state(int state);
void wearable_sync_host_set_time_synchronized(bool synchronized);
void wearable_sync_host_set_registered(bool registered);
void wearable_sync_host_set_session_token(const char *token);
void wearable_sync_host_set_device_id(const char *device_id);
void wearable_sync_host_set_base_url(const char *base_url);
void wearable_sync_host_set_copy_session_result(esp_err_t result);
void wearable_sync_host_set_identity_result(esp_err_t result);
void wearable_sync_host_set_base_url_result(esp_err_t result);
void wearable_sync_host_set_transport_security_result(esp_err_t result);
void wearable_sync_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
);
void wearable_sync_host_set_perform_result(esp_err_t result);
void wearable_sync_host_set_client_init_fails(bool fails);
void wearable_sync_host_set_header_failure(const char *key, esp_err_t result);
void wearable_sync_host_set_post_field_failure(esp_err_t result);
void wearable_sync_host_set_task_create_fails(bool fails);
void wearable_sync_host_set_stop_after_delays(int delay_count);
void wearable_sync_host_set_policy_store_fails(bool fails);
void wearable_sync_host_set_time_ms(int64_t now_ms);
void wearable_sync_host_advance_time_ms(int64_t delta_ms);
void wearable_sync_host_set_sos_triggered(bool triggered);

int wearable_sync_host_clear_session_calls(void);
int wearable_sync_host_transport_security_calls(void);
int wearable_sync_host_task_create_calls(void);
int wearable_sync_host_task_delete_calls(void);
int wearable_sync_host_delay_calls(void);
unsigned int wearable_sync_host_last_delay_ticks(void);
bool wearable_sync_host_task_pending(void);
int wearable_sync_host_policy_store_calls(void);

int wearable_sync_host_request_count(void);
const char *wearable_sync_host_request_url(size_t index);
const char *wearable_sync_host_request_authorization(size_t index);
const char *wearable_sync_host_request_accept(size_t index);
const char *wearable_sync_host_request_content_type(size_t index);
const char *wearable_sync_host_request_body(size_t index);
int wearable_sync_host_request_method(size_t index);

void wearable_sync_host_run_pending_task(void);

#ifdef __cplusplus
}
#endif
