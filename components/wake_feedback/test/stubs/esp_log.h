#pragma once

#include "esp_err.h"

// Referencing the tag keeps -Werror=unused-const-variable quiet when the
// source declares a TAG that only the no-op log macros would have used.
#define ESP_LOGI(tag, format, ...) ((void)(tag))
#define ESP_LOGW(tag, format, ...) ((void)(tag))
#define ESP_LOGE(tag, format, ...) ((void)(tag))

static inline const char *esp_err_to_name(esp_err_t error) {
    (void)error;
    return "esp_error";
}
