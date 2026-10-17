#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void vision_sync_host_reset(void);
void vision_sync_host_reset_module(void);
void vision_sync_host_clear_provider(void);
void vision_sync_host_set_stop(bool stop);
bool vision_sync_host_is_ready(void);

void vision_sync_host_set_network_state(int state);
void vision_sync_host_set_time_synchronized(bool synchronized);
void vision_sync_host_set_registered(bool registered);
void vision_sync_host_set_session_token(const char *token);
void vision_sync_host_set_device_id(const char *device_id);
void vision_sync_host_set_base_url(const char *base_url);
void vision_sync_host_set_copy_session_result(esp_err_t result);
void vision_sync_host_set_identity_result(esp_err_t result);
void vision_sync_host_set_base_url_result(esp_err_t result);
void vision_sync_host_set_transport_security_result(esp_err_t result);
void vision_sync_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
);
void vision_sync_host_set_perform_result(esp_err_t result);
void vision_sync_host_set_client_init_fails(bool fails);
void vision_sync_host_set_header_failure(const char *key, esp_err_t result);
void vision_sync_host_set_post_field_failure(esp_err_t result);
void vision_sync_host_set_camera_ready(bool ready);
void vision_sync_host_set_accept_result(int result);
void vision_sync_host_set_acknowledge_result(int result);
void vision_sync_host_set_camera_fail_result(int result);
void vision_sync_host_set_camera_complete_result(int result);
void vision_sync_host_set_task_create_fails(bool fails);
void vision_sync_host_set_stop_after_delays(int delay_count);

int vision_sync_host_clear_session_calls(void);
int vision_sync_host_transport_security_calls(void);
int vision_sync_host_task_create_calls(void);
int vision_sync_host_task_delete_calls(void);
int vision_sync_host_delay_calls(void);
unsigned int vision_sync_host_last_delay_ticks(void);
bool vision_sync_host_task_pending(void);

int vision_sync_host_request_count(void);
const char *vision_sync_host_request_url(size_t index);
const char *vision_sync_host_request_authorization(size_t index);
const char *vision_sync_host_request_accept(size_t index);
const char *vision_sync_host_request_content_type(size_t index);
const char *vision_sync_host_request_body(size_t index);
int vision_sync_host_request_method(size_t index);

int vision_sync_host_camera_accept_calls(void);
int vision_sync_host_camera_acknowledge_calls(void);
int vision_sync_host_camera_fail_calls(void);
const char *vision_sync_host_last_failure_code(void);

void vision_sync_host_run_pending_task(void);

#ifdef __cplusplus
}
#endif
