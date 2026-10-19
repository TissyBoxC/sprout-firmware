#pragma once

#include "esp_err.h"

typedef struct module_descriptor {
    const char *module_name;
    const char *version;
    esp_err_t (*initialize)(void);
    void (*shutdown)(void);
} module_descriptor_t;
