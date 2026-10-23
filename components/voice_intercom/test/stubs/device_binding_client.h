#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_BINDING_SESSION_TOKEN_SIZE 129

// Host-test stand-in for the device binding client. Only the two reads the
// intercom needs are provided: the call-state report takes the platform endpoint
// and the current session token, and nothing else on this surface is exercised.
esp_err_t device_binding_client_copy_session_token(
    char *output,
    size_t output_size
);

esp_err_t device_binding_client_get_platform_base_url(
    char *output,
    size_t output_size
);

#ifdef __cplusplus
}
#endif
