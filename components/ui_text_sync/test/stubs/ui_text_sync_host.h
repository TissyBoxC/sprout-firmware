#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "network_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_text_sync_host_reset(void);
void ui_text_sync_host_reset_store(void);

void ui_text_sync_host_set_network_state(network_manager_state_t state);
void ui_text_sync_host_set_time_synchronized(bool synchronized);
void ui_text_sync_host_set_registered(bool registered);
void ui_text_sync_host_set_copy_session_result(esp_err_t result);
void ui_text_sync_host_set_identity_result(esp_err_t result);
void ui_text_sync_host_set_base_url_result(esp_err_t result);
void ui_text_sync_host_set_perform_result(esp_err_t result);
void ui_text_sync_host_set_client_init_fails(bool fails);
void ui_text_sync_host_set_transport_security_result(esp_err_t result);
void ui_text_sync_host_set_store_string_result(esp_err_t result);
void ui_text_sync_host_set_store_blob_result(esp_err_t result);
void ui_text_sync_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
);

size_t ui_text_sync_host_request_count(void);
const char *ui_text_sync_host_request_url(size_t index);
esp_http_client_method_t ui_text_sync_host_request_method(size_t index);
const char *ui_text_sync_host_request_authorization(size_t index);
int ui_text_sync_host_clear_session_calls(void);
int ui_text_sync_host_transport_security_calls(void);

const char *ui_text_sync_host_cursor(void);
int ui_text_sync_host_set_string_calls(void);
int ui_text_sync_host_set_blob_calls(void);

void ui_text_sync_host_set_active_package(
    const char *package_id,
    const char *language,
    unsigned int package_version
);
void ui_text_sync_host_set_apply_result(esp_err_t result);
void ui_text_sync_host_set_withdraw_result(esp_err_t result);
int ui_text_sync_host_apply_calls(void);
int ui_text_sync_host_withdraw_calls(void);
const char *ui_text_sync_host_last_applied_package(void);
const char *ui_text_sync_host_last_withdrawn_package(void);
const char *ui_text_sync_host_stored_package(void);

#ifdef __cplusplus
}
#endif
