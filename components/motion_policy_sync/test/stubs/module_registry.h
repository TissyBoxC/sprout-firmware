#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*module_initialize_fn)(void);
typedef void (*module_shutdown_fn)(void);

typedef struct {
    const char *module_name;
    const char *version;
    module_initialize_fn initialize;
    module_shutdown_fn shutdown;
} module_descriptor_t;

#ifdef __cplusplus
}
#endif
