#pragma once

#include "esp_err.h"

typedef esp_err_t (*module_init_fn)(void);
typedef void (*module_shutdown_fn)(void);
typedef void (*module_failure_handler_t)(
    const char *module_name,
    esp_err_t error_code
);
typedef void (*module_recovery_handler_t)(const char *module_name);

typedef struct {
    const char *module_name;
    const char *version;
    module_init_fn initialize;
    module_shutdown_fn shutdown;
} module_descriptor_t;

void module_registry_set_failure_handler(module_failure_handler_t handler);
void module_registry_set_recovery_handler(module_recovery_handler_t handler);
