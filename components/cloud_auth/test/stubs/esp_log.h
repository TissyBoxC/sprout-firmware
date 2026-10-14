#pragma once

#include "esp_err.h"

#define ESP_LOGI(tag, format, ...) ((void)0)
#define ESP_LOGW(tag, format, ...) ((void)0)
#define ESP_LOGE(tag, format, ...) ((void)0)

static inline const char *esp_err_to_name(esp_err_t error) {
    (void)error;
    return "esp_error";
}
