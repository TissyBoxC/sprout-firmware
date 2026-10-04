/*
 * Host-test stub for the removable module registry.
 *
 * diagnostic_reporter.h exposes a module_descriptor_t factory, so the host
 * build needs the type to parse the header. The state tests never call into
 * the registry, so no function declarations are required.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef esp_err_t (*module_init_fn)(void);
typedef void (*module_shutdown_fn)(void);

typedef struct {
    const char *module_name;
    const char *version;
    module_init_fn initialize;
    module_shutdown_fn shutdown;
} module_descriptor_t;
