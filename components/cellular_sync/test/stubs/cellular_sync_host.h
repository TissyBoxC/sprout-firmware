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

void cellular_sync_host_reset(void);
void cellular_sync_host_set_network_state(network_manager_state_t state);
void cellular_sync_host_set_time_synchronized(bool synchronized);
void cellular_sync_host_set_registered(bool registered);
void cellular_sync_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
);
void cellular_sync_host_set_perform_result(esp_err_t result);
void cellular_sync_host_set_transport_result(esp_err_t result);
void cellular_sync_host_set_store_result(esp_err_t result);
size_t cellular_sync_host_request_count(void);
const char *cellular_sync_host_request_url(size_t index);
const char *cellular_sync_host_request_authorization(size_t index);
const char *cellular_sync_host_request_body(size_t index);
esp_http_client_method_t cellular_sync_host_request_method(size_t index);
int cellular_sync_host_clear_session_calls(void);
int cellular_sync_host_transport_calls(void);
int cellular_sync_host_store_calls(void);
bool cellular_sync_host_has_stored_policy(void);
uint32_t cellular_sync_host_stored_revision(void);

#ifdef __cplusplus
}
#endif
