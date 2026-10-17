#pragma once

#include <stddef.h>

#include "esp_err.h"

#define DEVICE_BINDING_SESSION_TOKEN_SIZE 96

#ifdef __cplusplus
extern "C" {
#endif

bool device_binding_client_is_registered(void);
esp_err_t device_binding_client_copy_session_token(
    char *output,
    size_t output_size
);
esp_err_t device_binding_client_clear_session_token(void);
esp_err_t device_binding_client_get_platform_base_url(
    char *output,
    size_t output_size
);

#ifdef __cplusplus
}
#endif
