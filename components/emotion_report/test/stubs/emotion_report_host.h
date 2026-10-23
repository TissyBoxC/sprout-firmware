#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "network_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Reset every dependency stub and the request log between scenarios. */
void emotion_report_host_reset(void);

void emotion_report_host_set_network_state(network_manager_state_t state);
void emotion_report_host_set_time_synchronized(bool synchronized);
void emotion_report_host_set_registered(bool registered);
void emotion_report_host_set_session_token(const char *token);
void emotion_report_host_set_device_id(const char *device_id);
void emotion_report_host_set_base_url(const char *base_url);
void emotion_report_host_set_copy_session_result(esp_err_t result);
void emotion_report_host_set_transport_security_result(esp_err_t result);
void emotion_report_host_set_perform_result(esp_err_t result);
void emotion_report_host_set_response_status(int status_code);
void emotion_report_host_set_client_init_fails(bool fails);
void emotion_report_host_set_task_create_fails(bool fails);
void emotion_report_host_set_now_epoch_seconds(int64_t now);

int64_t emotion_report_host_now_epoch_seconds(void);
int emotion_report_host_clear_session_calls(void);
int emotion_report_host_task_create_calls(void);
int emotion_report_host_task_delete_calls(void);
int emotion_report_host_delay_calls(void);
size_t emotion_report_host_request_count(void);
const char *emotion_report_host_request_url(size_t index);
const char *emotion_report_host_request_authorization(size_t index);
const char *emotion_report_host_request_body(size_t index);
esp_http_client_method_t emotion_report_host_request_method(size_t index);
bool emotion_report_host_task_pending(void);

#ifdef __cplusplus
}
#endif
