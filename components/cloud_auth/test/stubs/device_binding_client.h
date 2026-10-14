#pragma once

#include <stdbool.h>

#include "esp_err.h"

bool device_binding_client_is_registered(void);
esp_err_t device_binding_client_authenticate(void);
esp_err_t device_binding_client_clear_session_token(void);
