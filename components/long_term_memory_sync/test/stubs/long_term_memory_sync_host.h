#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "network_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

void long_term_memory_sync_host_reset(void);
void long_term_memory_sync_host_reset_store(void);

void long_term_memory_sync_host_set_network_state(network_manager_state_t state);
void long_term_memory_sync_host_set_time_synchronized(bool synchronized);
void long_term_memory_sync_host_set_registered(bool registered);
void long_term_memory_sync_host_set_session_token(const char *token);
void long_term_memory_sync_host_set_device_id(const char *device_id);
void long_term_memory_sync_host_set_base_url(const char *base_url);
void long_term_memory_sync_host_set_copy_session_result(esp_err_t result);
void long_term_memory_sync_host_set_identity_result(esp_err_t result);
void long_term_memory_sync_host_set_base_url_result(esp_err_t result);
void long_term_memory_sync_host_set_transport_security_result(esp_err_t result);
void long_term_memory_sync_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
);
void long_term_memory_sync_host_set_perform_result(esp_err_t result);
void long_term_memory_sync_host_set_client_init_fails(bool fails);
void long_term_memory_sync_host_set_task_create_fails(bool fails);
void long_term_memory_sync_host_set_stop_after_delays(int delay_count);

int long_term_memory_sync_host_clear_session_calls(void);
int long_term_memory_sync_host_transport_security_calls(void);
int long_term_memory_sync_host_task_create_calls(void);
int long_term_memory_sync_host_task_delete_calls(void);
int long_term_memory_sync_host_delay_calls(void);
bool long_term_memory_sync_host_task_pending(void);

size_t long_term_memory_sync_host_request_count(void);
const char *long_term_memory_sync_host_request_url(size_t index);
const char *long_term_memory_sync_host_request_authorization(size_t index);
const char *long_term_memory_sync_host_request_body(size_t index);
esp_http_client_method_t long_term_memory_sync_host_request_method(
    size_t index
);

void long_term_memory_sync_host_run_pending_task(void);

void long_term_memory_sync_host_reset_module(void);
void long_term_memory_sync_host_set_stop(bool stop);
bool long_term_memory_sync_host_is_ready(void);
bool long_term_memory_sync_host_is_pending(const char *memory_key);

#ifdef __cplusplus
}
#endif
