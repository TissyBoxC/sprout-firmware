#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *module_name;
    const char *version;
    esp_err_t (*initialize)(void);
    void (*shutdown)(void);
} module_descriptor_t;

#ifdef __cplusplus
}
#endif
