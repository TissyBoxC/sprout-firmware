#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    CONFIG_STORE_HOST_DEFAULT = 0,
    CONFIG_STORE_HOST_LARGE = 1,
} config_store_host_partition_t;

void config_store_host_reset_state(void);
void config_store_host_reset(void);
void config_store_host_set_large_partition_present(bool present);
void config_store_host_set_default_init_result(esp_err_t result);
void config_store_host_set_default_retry_result(esp_err_t result);
void config_store_host_set_default_erase_result(esp_err_t result);
void config_store_host_set_large_init_result(esp_err_t result);
void config_store_host_set_large_retry_result(esp_err_t result);
void config_store_host_set_large_erase_result(esp_err_t result);
void config_store_host_set_default_open_result(esp_err_t result);
void config_store_host_set_large_open_result(esp_err_t result);
void config_store_host_set_default_set_result(esp_err_t result);
void config_store_host_set_large_set_result(esp_err_t result);
void config_store_host_set_default_commit_result(esp_err_t result);
void config_store_host_set_large_commit_result(esp_err_t result);
void config_store_host_set_default_key_erase_result(esp_err_t result);
void config_store_host_set_large_key_erase_result(esp_err_t result);

int config_store_host_default_init_calls(void);
int config_store_host_default_erase_calls(void);
int config_store_host_large_init_calls(void);
int config_store_host_large_erase_calls(void);
int config_store_host_key_partition(const char *key);

bool config_store_host_seed_blob(
    config_store_host_partition_t partition,
    const char *key,
    const void *value,
    size_t value_size
);
bool config_store_host_seed_string(
    config_store_host_partition_t partition,
    const char *key,
    const char *value
);
