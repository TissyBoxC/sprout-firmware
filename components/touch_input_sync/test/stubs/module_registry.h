#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct module_descriptor {
    const char *module_name;
    const char *version;
    esp_err_t (*initialize)(void);
    esp_err_t (*shutdown)(void);
} module_descriptor_t;

#ifdef __cplusplus
}
#endif
