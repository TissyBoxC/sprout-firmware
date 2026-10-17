#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "module_registry.h"

#define DEVICE_IDENTIFIER_SIZE 32

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t device_identity_copy(char *output, size_t output_size);

#ifdef __cplusplus
}
#endif
