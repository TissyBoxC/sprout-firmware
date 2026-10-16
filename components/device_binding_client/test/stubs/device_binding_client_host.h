#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

void device_binding_client_host_reset(void);
void device_binding_client_host_reset_module(void);

void device_binding_client_host_set_config_ready(bool ready);
void device_binding_client_host_set_platform_url(const char *url);
void device_binding_client_host_set_identity_result(esp_err_t result);
void device_binding_client_host_set_public_key_result(esp_err_t result);
void device_binding_client_host_set_sign_result(esp_err_t result);
void device_binding_client_host_set_base64_result(int result);
void device_binding_client_host_set_transport_security_result(esp_err_t result);
void device_binding_client_host_set_client_init_fails(bool fails);
void device_binding_client_host_set_perform_result(esp_err_t result);
void device_binding_client_host_set_http_sequence(
    const int *status_codes,
    const char *const *bodies,
    size_t count
);
void device_binding_client_host_set_store_set_string_result(esp_err_t result);
void device_binding_client_host_set_store_erase_result(esp_err_t result);
void device_binding_client_host_set_app_version(const char *version);
void device_binding_client_host_seed_string(const char *key, const char *value);

int device_binding_client_host_transport_security_calls(void);
size_t device_binding_client_host_request_count(void);
const char *device_binding_client_host_request_url(size_t index);
const char *device_binding_client_host_request_body(size_t index);
const char *device_binding_client_host_request_authorization(size_t index);
esp_http_client_method_t device_binding_client_host_request_method(
    size_t index
);
const char *device_binding_client_host_stored_string(const char *key);
bool device_binding_client_host_has_key(const char *key);
const char *device_binding_client_host_last_signed_message(void);
size_t device_binding_client_host_last_signed_message_size(void);
int device_binding_client_host_base64_encode_calls(void);

/** Production hook compiled into device_binding_client.cpp for host tests. */
bool device_binding_client_host_is_ready(void);

#ifdef __cplusplus
}
#endif
