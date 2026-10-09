#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef int config_store_result_t;

#define CONFIG_STORE_ERR_NOT_FOUND 0x105

int config_store_get_blob(const char *key, void *output, size_t *size);
int config_store_set_blob(const char *key, const void *value, size_t size);
