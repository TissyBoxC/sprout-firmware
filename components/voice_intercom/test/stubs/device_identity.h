#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_IDENTIFIER_SIZE 64

esp_err_t device_identity_copy(char *device_id_out, size_t size);

#ifdef __cplusplus
}
#endif
