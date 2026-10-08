#pragma once

#include "esp_err.h"

typedef esp_err_t (*module_init_fn)(void);
typedef void (*module_shutdown_fn)(void);

typedef struct {
    const char *module_name;
    const char *version;
    module_init_fn initialize;
    module_shutdown_fn shutdown;
} module_descriptor_t;
