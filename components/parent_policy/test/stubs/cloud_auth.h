#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cloud_auth_ensure_authenticated(void);
esp_err_t cloud_auth_mark_unauthorized(void);
esp_err_t cloud_auth_mark_revoked(void);

#ifdef __cplusplus
}
#endif
