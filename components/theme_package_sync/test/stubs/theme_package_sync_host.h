#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void theme_package_sync_host_reset(void);
void theme_package_sync_host_reset_module(void);
void theme_package_sync_host_set_stop(bool stop);
void theme_package_test_reset_ready(void);

void theme_package_sync_host_set_network_state(int state);
void theme_package_sync_host_set_time_synchronized(bool synchronized);
void theme_package_sync_host_set_registered(bool registered);
void theme_package_sync_host_set_session_token(const char *token);
void theme_package_sync_host_set_device_id(const char *device_id);
void theme_package_sync_host_set_base_url(const char *base_url);
void theme_package_sync_host_set_copy_session_result(esp_err_t result);
void theme_package_sync_host_set_identity_result(esp_err_t result);
void theme_package_sync_host_set_base_url_result(esp_err_t result);
void theme_package_sync_host_set_transport_security_result(esp_err_t result);
void theme_package_sync_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
);
void theme_package_sync_host_set_perform_result(esp_err_t result);
void theme_package_sync_host_set_client_init_fails(bool fails);
void theme_package_sync_host_set_task_create_fails(bool fails);
void theme_package_sync_host_set_stop_after_delays(int delay_count);

int theme_package_sync_host_clear_session_calls(void);
int theme_package_sync_host_transport_security_calls(void);
int theme_package_sync_host_task_create_calls(void);
int theme_package_sync_host_task_delete_calls(void);
int theme_package_sync_host_delay_calls(void);
unsigned int theme_package_sync_host_last_delay_ticks(void);
bool theme_package_sync_host_task_pending(void);
bool theme_package_sync_host_is_ready(void);

int theme_package_sync_host_request_count(void);
const char *theme_package_sync_host_request_url(size_t index);
const char *theme_package_sync_host_request_authorization(size_t index);
int theme_package_sync_host_request_method(size_t index);

void theme_package_sync_host_run_pending_task(void);

#ifdef __cplusplus
}
#endif
