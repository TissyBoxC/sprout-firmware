#pragma once

// The registry logs on every lifecycle transition. The host test asserts on
// observable registry state instead, so the log macros compile to nothing.
#define ESP_LOGI(tag, format, ...) ((void)0)
#define ESP_LOGW(tag, format, ...) ((void)0)
#define ESP_LOGE(tag, format, ...) ((void)0)

// The registry formats ESP-IDF error names into failure logs. The stub returns
// a stable placeholder so the test never depends on the real catalog.
static inline const char *esp_err_to_name(esp_err_t error) {
    (void)error;
    return "esp_error";
}
