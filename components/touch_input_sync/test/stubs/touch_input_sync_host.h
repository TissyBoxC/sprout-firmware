#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "network_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

void touch_input_sync_host_reset(void);
void touch_input_sync_host_set_network_state(network_manager_state_t state);
void touch_input_sync_host_set_time_synchronized(bool synchronized);
void touch_input_sync_host_set_registered(bool registered);
void touch_input_sync_host_set_body(int status_code, const char *body);
void touch_input_sync_host_set_perform_result(esp_err_t result);
void touch_input_sync_host_set_transport_result(esp_err_t result);
void touch_input_sync_host_set_base_url(const char *base_url);
size_t touch_input_sync_host_request_count(void);
const char *touch_input_sync_host_request_url(size_t index);
const char *touch_input_sync_host_request_authorization(size_t index);
esp_http_client_method_t touch_input_sync_host_request_method(size_t index);
int touch_input_sync_host_clear_session_calls(void);
int touch_input_sync_host_transport_calls(void);

#ifdef __cplusplus
}
#endif
