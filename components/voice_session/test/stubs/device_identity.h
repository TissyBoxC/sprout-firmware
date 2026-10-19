#pragma once

#include <stddef.h>

#include "esp_err.h"

#define DEVICE_IDENTIFIER_SIZE 32

esp_err_t device_identity_copy(char *output, size_t output_size);
