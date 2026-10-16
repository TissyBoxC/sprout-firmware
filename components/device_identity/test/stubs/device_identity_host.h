#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

void device_identity_host_reset_state(void);
void device_identity_host_reset(void);
void device_identity_host_clear_persisted_key(void);
void device_identity_host_simulate_reboot(void);

void device_identity_host_set_mac(const uint8_t mac[6]);
void device_identity_host_set_crypto_init_result(int result);
void device_identity_host_set_partition_init_result(esp_err_t result);
void device_identity_host_set_partition_init_retry_result(esp_err_t result);
void device_identity_host_set_partition_init_persistent_failure(
    bool persistent
);
void device_identity_host_set_partition_erase_result(esp_err_t result);
void device_identity_host_set_nvs_open_result(esp_err_t result);
void device_identity_host_set_nvs_get_result(esp_err_t result);
void device_identity_host_set_nvs_set_result(esp_err_t result);
void device_identity_host_set_nvs_commit_result(esp_err_t result);

void device_identity_host_set_generate_key_result(int result);
void device_identity_host_set_export_key_result(int result);
void device_identity_host_set_export_key_size(size_t size);
void device_identity_host_set_import_key_result(int result);
void device_identity_host_set_export_public_key_result(int result);
void device_identity_host_set_export_public_key_size(size_t size);
void device_identity_host_set_hash_result(int result);
void device_identity_host_set_hash_digest(const uint8_t *digest, size_t size);
void device_identity_host_set_sign_result(int result);
void device_identity_host_set_sign_size(size_t size);

bool device_identity_host_key_persisted(void);
const uint8_t *device_identity_host_persisted_key(size_t *size_out);
int device_identity_host_generate_calls(void);
int device_identity_host_import_calls(void);
int device_identity_host_sign_calls(void);
int device_identity_host_destroy_calls(void);
int device_identity_host_partition_init_calls(void);
int device_identity_host_partition_erase_calls(void);
const uint8_t *device_identity_host_last_signed_message(
    size_t *size_out
);
